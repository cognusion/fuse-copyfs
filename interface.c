/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#ifdef linux
/* For pread()/pwrite() */
#define _XOPEN_SOURCE 500
#endif

#include <fuse.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <sys/statfs.h>
#include <sys/vfs.h>
#include <errno.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <attr/xattr.h>
#include <sys/time.h>
#include <time.h>

#include "helper.h"
#include "cache.h"
#include "structs.h"
#include "rcs.h"
#include "create.h"
#include "write.h"
#include "ea.h"

static int callback_getattr(const char *path, struct stat *st_data)
{
  metadata_t *metadata;
  version_t *version;
  char *rpath;
  int res;

  /* Use the real file */
  rpath = rcs_translate_path(path, rcs_version_path);
  if (!rpath)
    return -ENOENT;
  metadata = cache_get_metadata(path);
  version = rcs_find_version(metadata, LATEST, LATEST);
  res = lstat(rpath, st_data);
  free(rpath);

  if(res == -1)
    return -errno;

  /* Mix our metadata to the stat results */
  st_data->st_mode = (st_data->st_mode & ~0777) | version->v_mode;
  st_data->st_uid = version->v_uid;
  st_data->st_gid = version->v_gid;

  return 0;
}

static int callback_readlink(const char *path, char *buf, size_t size)
{
  char *rpath;
  int res;

  rpath = rcs_translate_path(path, rcs_version_path);
  if (!rpath)
    return -ENOENT;

  res = readlink(rpath, buf, size - 1);
  if (res == -1)
    {
      free(rpath);
      return -errno;
    }

  buf[res] = '\0';
  free(rpath);
  return 0;
}

#define METADATA_PREFIX "metadata."

static int callback_getdir(const char *path, fuse_dirh_t h, fuse_dirfil_t fill)
{
  struct dirent *entry;
  char *rpath;
  int res;
  DIR *dir;

  rpath = rcs_translate_path(path, rcs_version_path);
  dir = opendir(rpath);
  if (!dir)
    {
      free(rpath);
      return -errno;
    }

  /* Find the metadata files */
  do
    {
      entry = readdir(dir);
      if (entry)
	{
	  /*
	   * We want the metadata files (because a versionned file is
	   * behind them, the '.' and '..' directories, so that the listing
	   * looks reasonable.
	   */
	  if (!strcmp(entry->d_name, "metadata."))
	    {
	      /* This is the root's metadata, ignore it */
	      continue;
	    }
	  else if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, ".."))
	    {
	      res = fill(h, entry->d_name, 0);
	      if (res != 0)
		break;
	    }
	  else if (!strncmp(entry->d_name, METADATA_PREFIX,
			    strlen(METADATA_PREFIX)))
	    {
	      metadata_t *metadata;
	      char *file;

	      /* Check if the file is not currently in deleted state */
	      if (strcmp(path, "/"))
		file = helper_build_composite("SS", "/", path,
					      entry->d_name +
					      strlen(METADATA_PREFIX));
	      else
		file = helper_build_composite("-S", "/", entry->d_name +
					      strlen(METADATA_PREFIX));
	      metadata = rcs_translate_to_metadata(file, rcs_version_path);
	      free(file);
	      if (metadata && !metadata->md_deleted)
		{
		  res = fill(h, entry->d_name + strlen(METADATA_PREFIX), 0);
		  if (res != 0)
		    break;
		}
	    }
	}
    }
  while (entry);

  closedir(dir);
  free(rpath);
  return 0;
}

static int callback_mknod(const char *path, mode_t mode, dev_t rdev)
{
  return create_new_file(path, mode, fuse_get_context()->uid,
			 fuse_get_context()->gid, rdev);
}

static int callback_mkdir(const char *path, mode_t mode)
{
  return create_new_directory(path, mode, fuse_get_context()->uid,
			      fuse_get_context()->gid);
}

static int callback_unlink(const char *path)
{
  metadata_t *metadata;
  version_t *version;
  struct stat st_rfile;
  char *metafile;

  metadata = rcs_translate_to_metadata(path, rcs_version_path);
  if (!metadata || metadata->md_deleted)
    return -ENOENT;
  version = rcs_find_version(metadata, LATEST, LATEST);
  if (lstat(version->v_rfile, &st_rfile) == -1)
    return -errno;
  if (S_ISDIR(st_rfile.st_mode))
    return -EISDIR;
  metadata->md_deleted = 1;
  metafile = create_meta_name(metadata->md_vfile, "metadata");
  if (write_metadata_file(metafile, metadata) == -1) {
    free(metafile);
    return -errno;
  }
  free(metafile);
  return 0;
}

static int callback_rmdir(const char *path)
{
  metadata_t *dir_metadata;
  version_t *version;
  struct stat st_rfile;
  char *metafile;
  DIR *dir;
  struct dirent *entry;
  char *rpath;

  dir_metadata = rcs_translate_to_metadata(path, rcs_version_path);
  if (!dir_metadata || dir_metadata->md_deleted)
    return -ENOENT;
  version = rcs_find_version(dir_metadata, LATEST, LATEST);
  if (lstat(version->v_rfile, &st_rfile) == -1)
    return -errno;
  if (!S_ISDIR(st_rfile.st_mode))
    return -ENOTDIR;

  rpath = rcs_translate_path(path, rcs_version_path);
  dir = opendir(rpath);
  if (!dir) {
    free(rpath);
    return -errno;
  }

  /* Check if there is any file in the directory */
  do {
    entry = readdir(dir);
    if (entry) {
      /*
       * We want the metadata files (because a versionned file is
       * behind them, the '.' and '..' directories, so that the listing
       * looks reasonable.
       */
      if (!strcmp(entry->d_name, "metadata.")) {
	/* This is the root's metadata, ignore it */
	continue;
      } else if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
	continue ;
      } else if (!strncmp(entry->d_name, METADATA_PREFIX,
			    strlen(METADATA_PREFIX))) {
	metadata_t *metadata;
	char *file;

	/* Check if the file is not currently in deleted state */
	if (strcmp(path, "/"))
	  file = helper_build_composite("SS", "/", path,
					entry->d_name +
					strlen(METADATA_PREFIX));
	else
	  file = helper_build_composite("-S", "/", entry->d_name +
					strlen(METADATA_PREFIX));
	metadata = rcs_translate_to_metadata(file, rcs_version_path);
	free(file);
	if (metadata && !metadata->md_deleted) {
	  /* we found a file */
	  closedir(dir);
	  free(rpath);
	  return -ENOTEMPTY;
	}
      }
    }
  }
  while (entry);

  closedir(dir);
  free(rpath);

  dir_metadata->md_deleted = 1;
  metafile = create_meta_name(dir_metadata->md_vfile, "metadata");
  if (write_metadata_file(metafile, dir_metadata) == -1) {
    free(metafile);
    return -errno;
  }
  free(metafile);
  return 0;
}

static int callback_symlink(const char *from, const char *to)
{
  return create_new_symlink(from, to, fuse_get_context()->uid,
			    fuse_get_context()->gid);
}

static int callback_rename(const char *from, const char *to)
{
  /*
   * Not suppored, because there is always a fallback path, and we don't
   * version moves per se, so just let the calling program do the move
   * "manually".
   */
  (void)from;
  (void)to;
  return -EXDEV;
}

static int callback_link(const char *from, const char *to)
{
  /*
   * Forbid hard links, since there is no way to make them point to a new
   * version if need be.
   */
  (void)from;
  (void)to;
  return -EPERM;
}

static int callback_chmod(const char *path, mode_t mode)
{
  metadata_t *metadata;
  version_t *version;

  metadata = rcs_translate_to_metadata(path, rcs_version_path);
  if (!metadata)
    return -ENOENT;
  version = rcs_find_version(metadata, LATEST, LATEST);
  if (!version)
    return -ENOENT;

  if (create_new_subversion(path, mode, version->v_uid, version->v_gid) != 0)
    return -errno;
  return 0;
}

static int callback_chown(const char *path, uid_t uid, gid_t gid)
{
  metadata_t *metadata;
  version_t *version;

  metadata = rcs_translate_to_metadata(path, rcs_version_path);
  if (!metadata)
    return -ENOENT;
  version = rcs_find_version(metadata, LATEST, LATEST);
  if (!version)
    return -ENOENT;

  if (create_new_subversion(path, version->v_mode, uid, gid) != 0)
    return -errno;
  return 0;
}

static int callback_truncate(const char *path, off_t size)
{
    int res;
    char *rpath;
    metadata_t *metadata;

    if (create_new_version(path) == -1)
      return -errno;

    rpath = rcs_translate_path(path, rcs_version_path);
    metadata = cache_get_metadata(path);
    metadata->md_timestamp = time(NULL);
    res = truncate(rpath, size);
    if(res == -1)
      return -errno;

    return 0;
}

static int callback_utime(const char *path, struct utimbuf *buf)
{
  char *rpath;
  int res;

  rpath = rcs_translate_path(path, rcs_version_path);
  if (!rpath)
    return -ENOENT;

  res = utime(rpath, buf);
  if (res == -1)
    {
      free(rpath);
      return -errno;
    }

  free(rpath);
  return 0;
}

static int callback_open(const char *path, int flags)
{
  char *rpath;
  int res;

  if ((flags & O_WRONLY) || (flags & O_RDWR)) {
    if (create_new_version(path) == -1)
      return -errno;
  }

  rpath = rcs_translate_path(path, rcs_version_path);
  res = open(rpath, flags);
  free(rpath);
  if(res == -1)
    return -errno;
  close(res);
  return 0;
}

static int callback_read(const char *path, char *buf, size_t size, off_t off)
{
  char *rpath;
  int fd, res;

  rpath = rcs_translate_path(path, rcs_version_path);
  fd = open(rpath, O_RDONLY);
  if (fd == -1)
    {
      free(rpath);
      return -errno;
    }
  res = pread(fd, buf, size, off);
  if (res == -1)
    res = -errno;
  close(fd);
  free(rpath);
  return res;
}

static int callback_write(const char *path, const char *buf, size_t size,
                     off_t offset)
{
  int fd;
  int res;
  char *rpath;

  rpath = rcs_translate_path(path, rcs_version_path);
  fd = open(rpath, O_WRONLY);
  if(fd == -1)
    {
      free(rpath);
      return -errno;
    }
  res = pwrite(fd, buf, size, offset);
  if(res == -1)
    res = -errno;
  close(fd);
  free(rpath);
  return res;
}

static int callback_statfs(const char *path, struct statfs *st_buf)
{
  int res;

  res = statfs(path, st_buf);
  if (res == -1)
    return -errno;
  return 0;
}

static int callback_release(const char *path, int flags)
{
  /* No special treatment */
  (void) path;
  (void) flags;
  return 0;
}

static int callback_fsync(const char *path, int isdatasync)
{
  /* No special treatment */
  (void) path;
  (void) isdatasync;
  return 0;
}

struct fuse_operations callback_oper = {
    .getattr	= callback_getattr,
    .readlink	= callback_readlink,
    .getdir	= callback_getdir,
    .mknod	= callback_mknod,
    .mkdir	= callback_mkdir,
    .symlink	= callback_symlink,
    .unlink	= callback_unlink,
    .rmdir	= callback_rmdir,
    .rename	= callback_rename,
    .link	= callback_link,
    .chmod	= callback_chmod,
    .chown	= callback_chown,
    .truncate	= callback_truncate,
    .utime	= callback_utime,
    .open	= callback_open,
    .read	= callback_read,
    .write	= callback_write,
    .statfs	= callback_statfs,
    .release	= callback_release,
    .fsync	= callback_fsync,

    /* Extended attributes support for userland interaction */
    .setxattr	= callback_setxattr,
    .getxattr	= callback_getxattr,
    .listxattr	= callback_listxattr,
    .removexattr= callback_removexattr
};
