/*
 Author: Nisker
 Created: 14/11/2021
 Last Modified: 14/11/2021
 License: GPL-3.0

 */

 /*
 * TODOs:
 * add ram write function.
 */

#define FUSE_USE_VERSION 34
#define _GNU_SOURCE

#include <fuse_lowlevel.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <assert.h>
#include <pthread.h>
#include "gbxcart.h"
#include <stddef.h>

struct FileInfo nogame = {
	.size = 17,
	.name = "no game",
	.data = "Filled with data"
};

struct FileInfo nosave = {
	.size = 18,
	.name = "no save function",
	.data = "save data"
};

struct FileInfo dmp;
struct FileInfo dmp_save;
struct FileInfo ramOnlyFile = {0, "only reading ram"};

struct FileInfo *save = &nosave;
struct FileInfo *game = &nogame;
struct options options;

int condition = 0;

#define OPTION(t, p) { t, offsetof(struct options, p), 1 }
static const struct fuse_opt gbx_opts[] = {
	OPTION("--norom", ramOnly),
	OPTION("-n", ramOnly),
	OPTION("--reread", reread),
	OPTION("-e", reread),
	OPTION("--readonly", readonly),
	OPTION("-r", readonly),
	FUSE_OPT_END
};

static int file_stat(fuse_ino_t ino, struct stat *stbuf) {
	stbuf->st_ino = ino;
	switch (ino) {
	case 1:
		stbuf->st_mode = S_IFDIR | 0755;
		stbuf->st_nlink = 2;
		break;

	case GAME_INO:
		stbuf->st_mode = S_IFREG | 0444;
		stbuf->st_nlink = 1;
		stbuf->st_size = game->size;
		break;
			
	case SAVE_INO:
		stbuf->st_mode = S_IFREG | 0644;
		stbuf->st_nlink = 1;
		stbuf->st_size = save->size;
		break;
	default:
		return -1;
	}
	return 0;
}

static void fun_getattr(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {
	struct stat stbuf;

	(void) fi;

	memset(&stbuf, 0, sizeof(stbuf));
	if (file_stat(ino, &stbuf) == -1)
		fuse_reply_err(req, ENOENT);
	else
		fuse_reply_attr(req, &stbuf, 1.0);
}

static void fun_lookup(fuse_req_t req, fuse_ino_t parent, const char *name) {
	struct fuse_entry_param e;

	if (parent != 1 || (strcmp(name, game->name) && strcmp(name, save->name)) != 0)
		fuse_reply_err(req, ENOENT);
	else {
		if (!strcmp(name, game->name)){
			memset(&e, 0, sizeof(e));
			e.ino = GAME_INO;
			e.attr_timeout = 1.0;
			e.entry_timeout = 1.0;
			file_stat(e.ino, &e.attr);
		} else {
			memset(&e, 0, sizeof(e));
			e.ino = SAVE_INO;
			e.attr_timeout = 1.0;
			e.entry_timeout = 1.0;
			file_stat(e.ino, &e.attr);
		}

		fuse_reply_entry(req, &e);
	}
}

struct dirbuf {
	char *p;
	size_t size;
};

static void dirbuf_add(fuse_req_t req, struct dirbuf *b, const char *name, fuse_ino_t ino) {
	struct stat stbuf;
	size_t oldsize = b->size;
	b->size += fuse_add_direntry(req, NULL, 0, name, NULL, 0);
	b->p = (char *) realloc(b->p, b->size);
	memset(&stbuf, 0, sizeof(stbuf));
	stbuf.st_ino = ino;
	fuse_add_direntry(req, b->p + oldsize, b->size - oldsize, name, &stbuf, b->size);
}

#define min(x, y) ((x) < (y) ? (x) : (y))

static int reply_buf_limited(fuse_req_t req, const char *buf, size_t bufsize, off_t off, size_t maxsize) {
	if (off < bufsize)
		return fuse_reply_buf(req, buf + off,
				      min(bufsize - off, maxsize));
	else
		return fuse_reply_buf(req, NULL, 0);
}

static void fun_readdir(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi) {
	(void) fi;
	if (ino != 1)
		fuse_reply_err(req, ENOTDIR);
	else {
		struct dirbuf b;

		memset(&b, 0, sizeof(b));
		dirbuf_add(req, &b, ".", 1);
		dirbuf_add(req, &b, "..", 1);
		dirbuf_add(req, &b, game->name, GAME_INO);
		dirbuf_add(req, &b, save->name, SAVE_INO);
		reply_buf_limited(req, b.p, b.size, off, size);
		free(b.p);
	}
}

static void fun_open(fuse_req_t req, fuse_ino_t ino, struct fuse_file_info *fi) {

	if (ino != GAME_INO && ino != SAVE_INO)
		fuse_reply_err(req, EISDIR);
	//else if ((fi->flags & O_ACCMODE) != O_RDONLY)
	//	fuse_reply_err(req, EACCES);
	else
		fuse_reply_open(req, fi);
}

static void fun_read(fuse_req_t req, fuse_ino_t ino, size_t size, off_t off, struct fuse_file_info *fi) {
	(void) fi;

	if (ino == GAME_INO) {
		reply_buf_limited(req, game->data, game->size, off, size);
	}
	else if (ino == SAVE_INO) reply_buf_limited(req, save->data, save->size, off, size);
}

static void fun_write(fuse_req_t req, fuse_ino_t ino, const char *buf, size_t size, off_t off, struct fuse_file_info *fi) {
	(void) fi;

	if (ino == GAME_INO){
		fuse_reply_err(req, EACCES);						//Permission denied
	} 
	
	else if (ino == SAVE_INO) {
		if (save == &nosave) fuse_reply_err(req, EAGAIN); 	//if there is no save, tell user to retry later
		else if (size+off <= dmp_save.size){
			memcpy(dmp_save.data+off, buf, size);
			fuse_reply_write(req, size);
			condition = 1;
		} else fuse_reply_err(req, EFBIG);
	}

	else fuse_reply_err(req, ENOENT);
}

static void fun_unlink(fuse_req_t req, fuse_ino_t parent, const char *name){
	if (!strcmp(name, save->name))
	fuse_reply_err(req, 0);
	fuse_reply_err(req, EACCES);
}

static const struct fuse_lowlevel_ops fun_oper = {
	.lookup		= fun_lookup,
	.getattr	= fun_getattr,
	.readdir	= fun_readdir,
	.open		= fun_open,
	.read		= fun_read,
	.write		= fun_write,
	.unlink		= fun_unlink,
};



int main(int argc, char *argv[])
{
	struct fuse_args args = FUSE_ARGS_INIT(argc, argv);
	struct fuse_session *se;
	struct fuse_cmdline_opts opts;
	struct fuse_loop_config config;
	int ret = -1;

	if (fuse_opt_parse(&args, &options, gbx_opts, NULL) == -1)
		return 1;

	if (fuse_parse_cmdline(&args, &opts) != 0)
		return 1;
	
	if (opts.show_help) {
		printf("usage: %s [options] <mountpoint>\n\n", argv[0]);
		fuse_cmdline_help();
		fuse_lowlevel_help();
		printf("\n    -n   --norom           Read only the Save data\n");
		printf("    -r   --readonly        Read only mode\n");
		printf("    -e   --reread          Re-read the cartridge on reinsert\n");
		ret = 0;
		goto err_out1;
	} else if (opts.show_version) {
		printf("FUSE library version %s\n", fuse_pkgversion());
		fuse_lowlevel_version();
		ret = 0;
		goto err_out1;
	}

	if(opts.mountpoint == NULL) {
		printf("usage: %s [options] <mountpoint>\n", argv[0]);
		printf("       %s --help\n", argv[0]);
		ret = 1;
		goto err_out1;
	}
	
	if (gba()) {
		goto err_out1;
	}
	
	se = fuse_session_new(&args, &fun_oper, sizeof(fun_oper), &options);
	if (se == NULL)
	    goto err_out1;

	if (fuse_set_signal_handlers(se) != 0)
	    goto err_out2;

	if (fuse_session_mount(se, opts.mountpoint) != 0)
	    goto err_out3;
	
	fuse_daemonize(opts.foreground);
	
	/* Start thread to update file contents */

	pthread_t thread;
	int rc = pthread_create(&thread, NULL, Thandler, (void *) se);
	if (rc){
        fprintf(stderr, "pthread_create failed with %s\n", strerror(rc));
	}
	pthread_setname_np(thread, "Thandler");

	/* Block until ctrl+c or fusermount -u */
	if (opts.singlethread)
		ret = fuse_session_loop(se);
	else {
		config.clone_fd = opts.clone_fd;
		config.max_idle_threads = opts.max_idle_threads;
		ret = fuse_session_loop_mt(se, &config);
	}

	fuse_session_unmount(se);
err_out3:
	fuse_remove_signal_handlers(se);
err_out2:
	fuse_session_destroy(se);
err_out1:
	free(opts.mountpoint);
	fuse_opt_free_args(&args);

	return ret ? 1 : 0;
}
