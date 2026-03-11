#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/usb/usb_device.h>
#include <zephyr/fs/fs.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/logging/log.h>

#include <zephyr/net/socket.h>
#include <zephyr/fs/fs.h>

/* Lua Includes */
#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

LOG_MODULE_REGISTER(main);

/* 1. File System Setup (LittleFS) */
#include <zephyr/fs/littlefs.h>
FS_LITTLEFS_DECLARE_DEFAULT_CONFIG(storage);
static struct fs_mount_t lfs_storage_mount = {
    .type = FS_LITTLEFS,
    .fs_data = &storage,
    .storage_dev = (void *)FIXED_PARTITION_ID(storage_partition),
    .mnt_point = "/lfs",
};

/* 2. Lua Custom Print (Redirects to Zephyr Console) */
static int l_zephyr_print(lua_State *L) {
    int n = lua_gettop(L);
    for (int i = 1; i <= n; i++) {
        size_t len;
        const char *s = luaL_tolstring(L, i, &len);
        if (i > 1) printk("\t");
        printk("%s", s);
        lua_pop(L, 1);
    }
    printk("\n");
    return 0;
}

/* 3. Helper to Run Lua Strings */
void run_lua(lua_State *L, const char *code) {
    if (luaL_dostring(L, code) != LUA_OK) {
        printk("Error: %s\n", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
}

#include <zephyr/shell/shell.h>

static lua_State *L = NULL;

// This allows you to type: lua "print(10 + 20)" in the terminal
static int cmd_lua(const struct shell *sh, size_t argc, char **argv)
{
    if (L == NULL) {
        shell_error(sh, "Lua VM not initialized");
        return -ENOENT;
    }
    
    if (argc < 2) {
        shell_error(sh, "Usage: lua \"<code>\"");
        return -EINVAL;
    }

    // Execute the string passed from the terminal
    if (luaL_dostring(L, argv[1]) != LUA_OK) {
        shell_error(sh, "Lua Error: %s", lua_tostring(L, -1));
        lua_pop(L, 1); // Clean up the error message from stack
    }
    return 0;
}


SHELL_CMD_REGISTER(lua, NULL, "Run a Lua string", cmd_lua);


static int luaPanic(lua_State *L) {
    const char *msg = lua_tostring(L, -1);
    printk("!!! LUA PANIC !!! : %s\n", msg ? msg : "unknown error");
    return 0; // Return to hang or reset
}


/* --- 1. The Network Upload Thread --- */
void tcp_upload_thread(void) {
    int serv = zsock_socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in bind_addr = {
        .sin_family = AF_INET,
        .sin_port = htons(1234),
        .sin_addr.s_addr = htonl(INADDR_ANY),
    };
    
    zsock_bind(serv, (struct sockaddr *)&bind_addr, sizeof(bind_addr));
    zsock_listen(serv, 1);

    while (1) {
        /* Wait for a connection from your Ubuntu machine */
        int client = zsock_accept(serv, NULL, NULL);
        if (client < 0) continue;

        printk("\n[NET] Incoming Lua script detected...\n");
        
        struct fs_file_t file;
        fs_file_t_init(&file);
	fs_unlink("/lfs/main.lua");
	fs_open(&file, "/lfs/main.lua", FS_O_CREATE | FS_O_WRITE);
        fs_truncate(&file, 0); /* Clear the old file */

        char buf[256];
        int len;
	int total_rx = 0;
        int total_wr = 0;

        /* Receive data in chunks */
        while ((len = zsock_recv(client, buf, sizeof(buf), 0)) > 0) {
            total_rx += len;
            int wlen = fs_write(&file, buf, len);
	    printk("[upload] writing %d returned %d\n", len, wlen);
            if (wlen > 0) {
                total_wr += wlen;
            } else {
                printk("[FS] fs_write failed with error: %d\n", wlen);
            }
        }
        
        if (len < 0) {
            printk("[NET] Socket error during receive: %d\n", len);
        }

	int closeSts = fs_close(&file);
	printk("[upload] fs_close returns %d\n", closeSts);

        zsock_close(client);
	printk("[upload] Received: %d bytes | Wrote: %d bytes\n",
	       total_rx, total_wr);
    }
}
/* Tell Zephyr to run this function constantly in the background */
K_THREAD_DEFINE(upload_tid, 4096, tcp_upload_thread, NULL, NULL, NULL, 7, 0, 0);


/* --- 2. The Command to RUN the saved file --- */
static int cmd_luarun(const struct shell *sh, size_t argc, char **argv) {
    char *nameP = (argc >= 2) ? argv[1] : "/lfs/main.lua";
    printk("[luarun] running '%s'\n", nameP);

    struct fs_file_t file;
    fs_file_t_init(&file);
    if (fs_open(&file, nameP, FS_O_READ) < 0) {
        shell_error(sh, "Could not open %s", nameP);
        return -ENOENT;
    }
    
    /* Get file size */
    struct fs_dirent entry;
    fs_stat(nameP, &entry);
    size_t size = entry.size;
    
    /* Read the whole file into RAM (STM32H7 has plenty!) */
    char *script_buf = k_malloc(size + 1);
    fs_read(&file, script_buf, size);
    script_buf[size] = '\0'; /* Null-terminate the string */
    fs_close(&file);
    
    /* Execute it in our global Lua State (L) */
    if (luaL_dostring(L, script_buf) != LUA_OK) {
        shell_error(sh, "Lua Error: %s", lua_tostring(L, -1));
        lua_pop(L, 1);
    }
    
    k_free(script_buf);
    return 0;
}
SHELL_CMD_REGISTER(luarun, NULL, "Run a script: luarun /lfs/main.lua", cmd_luarun);


// Stubs
int _link() {return -1;}
int _unlink() {return -1;}
int _times() {return -1;}

int main(void) {
    int ret;

    /* A. Initialize USB CDC ACM for the Console */
    const struct device *dev = DEVICE_DT_GET(DT_CHOSEN(zephyr_console));
    if (usb_enable(NULL)) {
        return 0;
    }

    /* Wait for DTR (optional: waits for you to open a terminal) */
    uint32_t dtr = 0;
    while (!dtr) {
        uart_line_ctrl_get(dev, UART_LINE_CTRL_DTR, &dtr);
        k_msleep(100);
    }

    /* --- Mount File System --- */
    int fs_res = fs_mount(&lfs_storage_mount);
    if (fs_res != 0) {
        printk("LittleFS mount failed (%d). Formatting...\n", fs_res);
        
        /* Format the partition */
        fs_res = fs_mkfs(FS_LITTLEFS, (uintptr_t)FIXED_PARTITION_ID(storage_partition), &storage, 0);
        if (fs_res == 0) {
            printk("Format successful. Mounting...\n");
            fs_res = fs_mount(&lfs_storage_mount);
        } else {
            printk("Format failed with error %d\n", fs_res);
        }
    }
    
    if (fs_res == 0) {
        printk("LittleFS mounted successfully at /lfs\n");
    }    

    /* C. Initialize Lua State */
    L = luaL_newstate();
    lua_atpanic(L, luaPanic);
    luaL_openlibs(L);
    lua_register(L, "print", l_zephyr_print);

    printk("--- Lua 5.4 Virtual Machine Ready ---\n");

    /* D. Execute a Test Script */
    run_lua(L, "print('Hardware: STM32H753ZI')");
    run_lua(L, "print('Memory Test: ', 2^20 / 1024, 'KB')");

    /* Main loop - usually you'd launch a Shell or HTTP thread here */
    while (1) {
        k_sleep(K_FOREVER);
    }

    lua_close(L);
    return 0;
}
