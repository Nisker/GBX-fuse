# GBX-fuse
Filesystem in Userspace for the GBxCart RW by insidegadgets.

This made for Linux machines, i have not tested on OSX but it should be possible to make it work with osxfuse.
## Building
**Dependenices:** Fuse3

Build by cloneing and run make:
```bash
git clone https://github.com/Nisker/GBX-fuse
cd GBX-fuse
make
```
## Usage
Fuse relies on an existing folder to mount to, so first step is to make one with a nice name.
```bash
mkdir GAMEBOY
```
Next step is to run the program
```bash
./gbxfuse GAMEBOY/
```
Now the GBxCart wil be mounted like any other drive, if you attatch a game to the GBxCart it will be shown in the mountpoint folder.
Games can be switched out or removed as long as the R/W led is not lit.


**Limitaions**  
As of now it will not write saves back to the catridge, this means that if you play games directly from the the mountpoint the savefile have to be backed-up and written to the cartdridge by another program.
## Links
The GBxCart RW can be had at: <https://shop.insidegadgets.com>  
The original GBxCart repo: <https://github.com/insidegadgets/GBxCart-RW>
