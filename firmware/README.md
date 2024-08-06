This is the code for a PDP11 simulation on an ESP32. It also contains some code for turning
an attached LCD into a VT52/IE15 terminal as well as using a BlueTooth keyboard as an input device.

Flash it onto a board, turn it on. If you want to use a keyboard, make sure it's discoverable
over BlueTooth within the first few seconds of the ESP32 turning on. Note that the paired
BT device afterwards will be detected automatically. Unfortunately there's no easy way to clear
the pairing to use a different keyboard without erasing the flash of the ESP32 (idf.py erase-flash).

This firmware works on either the final hardware (as detailed in the ../pcb directory) as well
as a standard ESP32-Wrover-Kit development board + LCD and a modified Sunton 2432S028 board. You can configure which board to run on
in menuconfig. (If your LCDs backlight doesn't turn on, you selected the wrong one.)

The firmware embeds a floppy disk containing RTX-11 as well as Tetris, and will start this up
automatically unless a FAT-formatted micro-SD-card with a file called 'rq.dsk' is detected. If
this file is detected, the firmware will configure a larger PDP11 and will start from the disk.
A pre-configured 2.11BSD disk has been provided in ../hard-disk-image.

To quicken uploads (if you're developing the code), you can comment out the
spiffs_create_partition_image line in CMakeLists.txt. This'll stop 'idf.py flash'
from uploading the Tetris floppy every time.

This firmware is developed on the current (11 jan 2021) master branch of ESP-IDF, but will likely
compile on ESP-IDF 4.2 and possibly other versions.

As of April 2024 it is patched for latest ESP-IDF 5.2.1 and compiles with that. 

