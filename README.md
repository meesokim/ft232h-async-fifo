# ft232h-async-fifo

## What is this?
This repo contains the needed settings and code to use an FTDI FT232H as an *asynchronous* FIFO for data transfer from a FPGA (e.g.) to the PC (one direction only). The software was written from scratch using libusb.

## Licence and disclaimer
The content of this repo is licenced under AGPLv3+ and comes WITHOUT ANY WARRANTY! It is *really* experimental. I did not disassemble any proprietary stuff.

## Why?
I needed a way to shove parallel data (from a FPGA) through USB to the PC and this as fast as possible (good old UART is simple but slow!). I had a FT232H board laying around and at first glance it looked promising. However using Linux and not wanting to use proprietary stuff i had a *really* hard time making all this work (more or less, read on). Inside this repo i summarize my findings and provide some code to hopefuly help others getting started easier.

## Limitations
Please be aware that *asynchronous* FIFO mode is quite limited in speed and not reliable *at all* (there *will* be some data loss). For stuff like video/audio this can be acceptable, but if you are transfering e.g. memory dumps it is clearly not the mode you want.  
The FT232H supports a much faster and - i guess - more reliable *synchronous* FIFO mode, however it is much more difficult to use (at least for people like me who really have a hard time with FPGA-stuff) as you need to synchronize all transfers to a (in this case 60MHz) clock that is *provided by the FT232H*.

## First hurdle: EEPROM-settings
To use the FT232H in asynchronous FIFO mode you need (or do you??) to program the EEPROM that is (hopefully) attached to the IC. On my board (from Aliexpress) it is a [Microchip 93LC56B](https://www.microchip.com/en-us/product/93lc56b) which is a "2Kb Microwire (3-wire) Serial EEPROM with 16-Bit Organization".
### How to (not) brick your device
Caution: If you use `ftdi_eeprom` (from package `ftdi-eeprom` on Debian) **always** specify a valid VID and PID inside the config file you use. If you don't do this the (stupid!) tool will assume 0x0000/0x0000 and effectively *brick your board*. It will still be visible by `lsusb` but i was unable to erase the EEPROM with any tool i tried. Finally i had to desolder the (tiny - SOT23-6) EEPROM, solder it to an adapter board, use a Bus Pirate to erase it and solder it back. Yeah...
### What mode is the right one?
To avoid messing things up again i finally used the official FT_PROG tool inside a Windows-VM. The right setting is "245 FIFO". Maybe - i am not sure, i did - you also need to set the driver-setting to D2XXX.  
  
TODO: How to program the EEPROM correctly with the open `ftdi_eeprom`?

## Second hurdle: libFTDI
As i wanted to avoid proprietary stuff (as always) and because FTDI does not provide FOSS drivers i had to ressort to code based on reverse engineering, that is [libFTDI](https://www.intra2net.com/en/developer/libftdi/). This stuff is free as in freedom but not so greatly documented imho. It still provides - in addition to `ftdi_eeprom` - a tool `ftdi_stream` *but* this is only for *synchronous* FIFO mode!  
At this point i was stuck, so i wrote my own code from scratch, using only `libusb` and entirely avoiding `libftdi`.

## Third hurdle: Always read the documentation!
If you want to use the FT232H in FIFO mode you really need to carefuly read the [Technical Note 167](https://www.ftdichip.com/Support/Documents/TechnicalNotes/TN_167_FIFO_Basics.pdf) from FTDI. On page 12 there is a timing diagram for writing to the FT232H (that is sending data to the PC). If i understand this stuff correctly #WR is limited to 10MHz (period 100ns) which already severely impacts the possible throughput.

## Wiring up the FT232H board
Beware of "inverted" logic for the control/status lines!  
AD0-7 are the parallel inputs. AC2 is #RD and connected to Vcc (3,3V) as we only do writes. AC3 is #WR and connected to the FPGA or similar, a falling edge will trigger a write. AC1 is #TXE, monitor this signal with a scope (or your FPGA), if it goes high for more than 400ns you can't send more data (and/or you lost some already).

## My tool
### Dependencies
You need `libusb`, on Debian and derived distributions try `sudo apt install libusb-1.0-0-dev`.
### Adjustments in code
There are 4 `#define` that you can mess with before compiling:
* SIZE_BUF: The blocksize used for USB transfers. 512 bytes gave best results during my tests.
* LATENCY_TIMER_VALUE: The so called "latency timer". I did not dig too much into this, as far as i understood this is more important for small, occasional transfers; not continous streaming. I set this to max (255ms) most of the time.
* NB_PAR_TRANSFERS: Number of "parallel" (thats not the right word...) transfers used by the tool/libusb. Something between 8 and 16 (or smaller for occasional transfers) should be fine.

If you need maximum throughput (and can't use the synchronous mode) you will need to experiment with these parameters, at least the last 2.
* RUN_FOR_SECONDS: The number of seconds the program should run before exiting. Set this to 0 to run forever/until Ctrl+C.
### How to compile
`gcc -Wall -Wextra -Werror -I/usr/include/libusb-1.0 -O3 -o ft232h_rx main.c -lusb-1.0`
### How to use
The tool does not accept any arguments. It will print some messages to `stderr` while opening the first FT232H (0x0403/0x6014) found and then spit out received data on `stdout`. For my tests i either used something like `./ft232h_rx | pv -trbW  > /dev/null` or `./ft232h_rx | pv -trbW  > file.bin`. `pv` is a handy tool that will show the passed time, the total size of data that passed the pipe and the calculated throughput.

## Results?
Well, as i said above it is not great and highly unpredictable. I did most of my tests with a clock of 7,5MHz on #WR (which equals to a bit more than 7MiB/s throughput) and a LFSR running on the FPGA. Sometimes i had buffer overflows (#TXE high for >400ns) really quick (less than a minute), sometimes (not often) i was able to stream data without problems for several minutes. I randomly checked the received data using some C-code and it was correct. If you want the Verilog code and the piece of C code to generate the LFSR data open an issue, but please be aware that it is hacked together and i am really bad with Verilog/FPGA-stuff...  
I did not test "burst writes" as this is not what i am currently interested in / what i currently need.

## And so?
If you *really* need the *asynchronous* FIFO mode you will need to experiment and probably have a bad time. If possible avoid this mode and use *synchronous* FIFO mode instead.
