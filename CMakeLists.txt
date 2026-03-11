cmake_minimum_required(VERSION 3.20)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(stm32_lua_system)

# Ensure lparser.c is here!
set(LUA_SOURCES
    lua/lapi.c lua/lcode.c lua/lctype.c lua/ldebug.c lua/ldo.c 
    lua/ldump.c lua/lfunc.c lua/lgc.c lua/llex.c lua/lmem.c 
    lua/lobject.c lua/lopcodes.c lua/lstate.c lua/lstring.c 
    lua/ltable.c lua/ltm.c lua/lundump.c lua/lvm.c lua/lzio.c
    lua/lauxlib.c lua/lbaselib.c lua/lcorolib.c lua/ldblib.c 
    lua/liolib.c lua/lmathlib.c lua/loadlib.c lua/loslib.c 
    lua/lstrlib.c lua/ltablib.c lua/lutf8lib.c lua/linit.c
    lua/lparser.c
)

target_sources(app PRIVATE src/main.c ${LUA_SOURCES})
target_include_directories(app PRIVATE lua)

# THE FIX: Tell the linker to allow multiple definitions of symbols like 'remove'
target_link_options(app PUBLIC "-Wl,--allow-multiple-definition")

# Link math library
target_link_libraries(app PUBLIC -lm)
