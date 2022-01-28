#define FUSE_USE_VERSION 34

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include "setup.h"
#include <dirent.h> 

#define GAME_INO 2
#define SAVE_INO 3
#define notify_inode(inode) assert(fuse_lowlevel_notify_inval_inode(se, inode, 0, 0) == 0)

extern struct options {
	int ramOnly;
	int reread;
	int readonly;
	const char *filename;
	const char *cache_path;
} options;

extern unsigned int save_reserved_mem;
extern unsigned int game_reserved_mem;

struct FileInfo {
	unsigned int size;
	char name[20];
	char *data;
};

extern struct FileInfo dmp;
extern struct FileInfo dmp_save;
extern struct FileInfo ramOnlyFile;

extern struct FileInfo nogame;
extern struct FileInfo nosave;

extern struct FileInfo *save;
extern struct FileInfo *game;

extern int condition;

int gba();

void *Thandler(void *ptr);