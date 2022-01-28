# GBX-fuse
Filesystem in Userspace for the GBxCart RW by insidegadgets.

This is made for Linux machines, i have not tested on OSX, but it should be possible to make it work with osxfuse.
## Building
**Dependencies:** Fuse3 gcc make

Build by cloning and running make:
```bash
git clone https://github.com/Nisker/GBX-fuse
cd GBX-fuse
make
```
## Usage
Fuse relies on an existing folder to mount to. First step is to make one with a nice name.
```bash
mkdir GAMEBOY
```
Next step is to run the program.
```bash
./gbxfuse GAMEBOY/
```
Now the GBxCart will be mounted like any other storage device, if you attach a game to the GBxCart a ROM and savefile will be found in the mountpoint folder.  
Games can be switched out or removed as long as the Tx/Rx LED is not lit.  
A list of argumenst can be found by running `./gbxfuse --help`  

If you want it to load the ROM from a cache folder instead of rereading on each insert, a cache folder can be specified.  
Cachefolder needs a full path (like /home/user/roms), the user should also have read and write access to it.  
An exmample of this with an existing folder called 'roms' your users home folder:
```bash
./gbxfuse --cache=/home/$USER/roms GAMEBOY/
```


## Links
The GBxCart RW can be had at: <https://shop.insidegadgets.com>  
The original GBxCart repo: <https://github.com/insidegadgets/GBxCart-RW>
