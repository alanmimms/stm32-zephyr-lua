# Lua for ZephyrOS on STM32H753 NUCLEO board

I built this to familiarize myself with STM32H753 and ZephyrOS. it has
a LittleFS file system in the 1MB of extra room on my STM32H753ZI
chip. It supports TCP upload to port 1234 like this, after booting the
board with Ethernet attached and a serial console 115200 baud on
/dev/ttyACM1 (/dev/ttyACM0 is the flash and power port):

	nc ipaddr 1234 < t.lua
	
where _ipaddr_ is the IP address the board has. The file is uploaded
and stored in the fixed path `/lfs/main.lua` for now. You can find
_ipaddr_ by typing

	net iface
	
on the serial console. There is also `help` to show you all the little
shell can do.

After you have uploaded a file you can type

	luarun
	
or

	luarun /lfs/main.lua
	
to run the upload. There are also shell `fs` commands to let you move
and copy and cat files.

Have fun!
Alan Mimms
WB7NAB
alanmimms@gmail.com
