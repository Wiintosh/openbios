/*
 *	/packages/fatfs-files
 *
 *	fatfs interface
 *
 *   Copyright (C) 2001-2004 Samuel Rydh (samuel@ibrium.se)
 *   Copyright (C) 2010 Mark Cave-Ayland (mark.cave-ayland@siriusit.co.uk)
 *   Copyright (C) 2025 John Davis
 *
 *   This program is free software; you can redistribute it and/or
 *   modify it under the terms of the GNU General Public License
 *   as published by the Free Software Foundation
 *
 */

#include "config.h"
#include "libopenbios/bindings.h"
#include "fs/fs.h"
#include "ff.h"
#include "diskio.h"
#include "libc/vsprintf.h"
#include "libc/diskio.h"

extern void fatfs_init(void);

typedef struct {
    int     fd;
    FATFS   fs;
    char    vol_path[16];

    enum { FAT_FILE, FAT_DIR } type;
    union {
        FIL file;
        DIR dir;
    };
} fatfs_info_t;

DECLARE_NODE( fatfs, 0, sizeof (fatfs_info_t), "+/packages/fatfs-files" );

static void
print_date(WORD fdate, WORD ftime)
{
    unsigned int second, minute, hour, month, day, year;

    year    = 1980 + ((fdate >> 9) & 0x7F);
    month   = (fdate >> 5) & 0xF;
    day     = fdate & 0x1F;

    hour    = (fdate >> 11) & 0x1F;
    minute  = (fdate >> 5) & 0x3F;
    second  = (fdate & 0x1F) * 2;

    forth_printf("%d-%02d-%02d %02d:%02d:%02d ",
                year, month, day, hour, minute, second);
}

/* ( -- success? ) */
static void
fatfs_files_open(fatfs_info_t *mi)
{
    int     fd;
    char    *path = my_args_copy();
    char    buf[256];

    if (!path) {
        RET(0);
    }

    fd = open_ih(my_parent());
    if (fd == -1) {
        free(path);
        RET(0);
    }
    mi->fd = fd;

    snprintf(mi->vol_path, sizeof (mi->vol_path), "%d:", fd);
    if (f_mount(&mi->fs, mi->vol_path, 1) != FR_OK) {
        free(path);
        close_io(fd);
        RET(0);
    }

    snprintf(buf, sizeof (buf), "%d:%s", fd, path);
    if (f_opendir(&mi->dir, buf) != FR_OK) {
        if (f_open(&mi->file, buf, FA_READ) != FR_OK) {
            free(path);
            close_io(fd);
            RET(0);
        }
        mi->type = FAT_FILE;
        free(path);
        RET(-1);
    }

    mi->type = FAT_DIR;
    free(path);
    RET(-1);
}

/* ( -- ) */
static void
fatfs_files_close(fatfs_info_t *mi)
{
    if (mi->type == FAT_FILE) {
        f_close(&mi->file);
    } else if (mi->type == FAT_DIR) {
        f_closedir(&mi->dir);
    }

    f_unmount(mi->vol_path);
    close_io(mi->fd);
}

/* ( buf len -- actlen ) */
static void
fatfs_files_read(fatfs_info_t *mi)
{
    int count = POP();
    char *buf = (char *)cell2pointer(POP());
    UINT br;

    if (mi->type != FAT_FILE) {
        RET(-1);
    }

    if (f_read(&mi->file, buf, count, &br) != FR_OK) {
        RET(-1);
    }
    RET(br);
}

/* ( pos.d -- status ) */
static void
fatfs_files_seek(fatfs_info_t *mi)
{
	long long pos = DPOP();
    if (mi->type != FAT_FILE) {
        RET(-1);
    }

    if (f_lseek(&mi->file, pos) != FR_OK) {
        RET(-1);
    }
    RET(0);
}

/* ( addr -- size ) */
static void
fatfs_files_load(fatfs_info_t *mi)
{
    char *buf = (char*)cell2pointer(POP());
    UINT br;

    if (mi->type != FAT_FILE) {
        RET(-1);
    }

    f_lseek(&mi->file, 0);
    if (f_read(&mi->file, buf, f_size(&mi->file), &br) != FR_OK) {
        RET(-1);
    }
    RET(br);
}

/* static method, ( pathstr len ihandle -- ) */
static void
fatfs_files_dir(fatfs_info_t *dummy)
{
    FATFS   fs;
    DIR     dir;
    FILINFO fno;
    int     fd;
    char    buf[256];

    ihandle_t ih = POP();
    char *path = pop_fstr_copy();

    fd = open_ih(ih);
    if (fd == -1) {
        free(path);
        return;
    }

    snprintf(buf, sizeof (buf), "%d:", fd);
    if (f_mount(&fs, buf, 1) != FR_OK) {
        free(path);
        close_io(fd);
        return;
    }

    snprintf(buf, sizeof (buf), "%d:%s", fd, (path != NULL) ? path : "\\");
    if (f_opendir(&dir, buf) != FR_OK) {
        free(path);
        close_io(fd);
        return;
    }

    forth_printf("\n");
    bzero(&fno, sizeof (fno));
    while (f_readdir(&dir, &fno) == FR_OK) {
        if (fno.fname[0] == 0) {
            break;
        }

        forth_printf("% 10lld ", fno.fsize);
        print_date(fno.fdate, fno.ftime);
        if (fno.fattrib & AM_DIR) {
            forth_printf("%s\\\n", fno.fname);
        } else {
            forth_printf("%s\n", fno.fname);
        }
        bzero(&fno, sizeof (fno));
    }

    f_closedir(&dir);
    f_unmount(buf);

    close_io(fd);

    free(path);
}

/* static method, ( pos.d ih -- flag? ) */
static void
fatfs_files_probe(fatfs_info_t *dummy)
{
    ihandle_t ih = POP_ih();
    long long offs = DPOP();
    int fd, ret = 0;
    FATFS   *fs;
    char    buf[16];

    fs = malloc(sizeof (FATFS));
    if (!fs) {
        RET(ret);
    }

    fd = open_ih(ih);
    if (fd >= 0) {
        snprintf(buf, sizeof (buf), "%d:", fd);
        fatfs_disk_offset = offs;
        if (f_mount(fs, buf, 1) == FR_OK) {
            f_unmount(buf);
            ret = -1;
        }
        fatfs_disk_offset = 0;
        close_io(fd);
    } else {
        ret = -1;
    }

    free(fs);
    RET(ret);
}

static void
fatfs_files_get_fstype(fatfs_info_t *dummy)
{
	PUSH( pointer2cell(strdup("FAT")) );
}

static void
fatfs_initializer(fatfs_info_t *dummy)
{
    fword("register-fs-package");
}

NODE_METHODS( fatfs ) = {
    { "probe",  fatfs_files_probe   },
    { "open",   fatfs_files_open    },
    { "close",  fatfs_files_close   },
    { "read",   fatfs_files_read    },
    { "seek",   fatfs_files_seek    },
    { "load",   fatfs_files_load    },
    { "dir",    fatfs_files_dir     },

    /* special */
	{ "get-fstype", fatfs_files_get_fstype	},

    { NULL,     fatfs_initializer   },
};

void
fatfs_init(void)
{
    REGISTER_NODE( fatfs );
}
