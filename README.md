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
Now the GBxCart will be mounted like any other drive, if you attach a game to the GBxCart it will be shown in the mountpoint folder.  
Games can be switched out or removed as long as the R/W led is not lit.  
If run with the -r argument it will launch in read-only mode and no data will be written back to the cart.


## Links
The GBxCart RW can be had at: <https://shop.insidegadgets.com>  
The original GBxCart repo: <https://github.com/insidegadgets/GBxCart-RW>
