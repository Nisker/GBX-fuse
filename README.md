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
Help can be found by running
```bash
./gbxfuse --help
```
## Links
The GBxCart RW can be had at: <https://shop.insidegadgets.com>  
The original GBxCart repo: <https://github.com/insidegadgets/GBxCart-RW>