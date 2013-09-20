/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <time.h>

#include "helper.h"
#include "structs.h"
#include "write.h"
#include "rcs.h"
#include "create.h"
#include "cache.h"

/*
 * Build a version file name with the given serial for the given virtual file.
 */
static char *create_version_name(const char *vpath, int serial)
{
  char buffer[9], *filename, *dirname, *translated, *cfile, *complete;

  filename = helper_extract_filename(vpath);
  dirname = helper_extract_dirname(vpath);

  /* Get the directory's path translation */
  translated = rcs_translate_path(dirname, rcs_version_path);
  assert(translated != NULL);

  /* Build the version file name */
  snprintf(buffer, 9, "%08X", serial);
  cfile = helper_build_composite("SS", ".", buffer, filename);

  complete = helper_build_composite("SS", "/", translated, cfile);

  free(translated);
  free(cfile);
  free(dirname);
  free(filename);
  return complete;
}

/*
 * Build a metadata file name with the given virtual file and prefix.
 * prefix is "metadata" for metadata file and "dfl-meta" for default file.
 */
char *create_meta_name(char *vpath, char *prefix)
{
  char *dir, *file, *xlat, *name, *res;

  dir = helper_extract_dirname(vpath);
  file = helper_extract_filename(vpath);
  xlat = rcs_translate_path(dir, rcs_version_path);
  name = helper_build_composite("SS", ".", prefix, file);
  res = helper_build_composite("SS", "/", xlat, name);
  free(xlat);
  free(name);
  free(file);
  free(dir);
  return res;
}

/*
 * Link a version to a metadata structure, and flush changes to disk. It will
 * only link the version in memory if the changes were successfully committed
 * to disk first.
 */
static int create_link_version(metadata_t *metadata, version_t *version)
{
  char *metafile, *dflfile;
  int old_vid, old_svid, old_deleted;

  /* Build the path for the metadata and default files */
  metafile = create_meta_name(metadata->md_vfile, "metadata");
  dflfile = create_meta_name(metadata->md_vfile, "dfl-meta");

  /* Link in memory */
  version->v_next = metadata->md_versions;
  metadata->md_versions = version;

  /* Remove the version lock */
  old_vid = metadata->md_dfl_vid;
  old_svid = metadata->md_dfl_svid;
  metadata->md_dfl_vid = LATEST;
  metadata->md_dfl_svid = LATEST;

  /* We are not deleted anymore */
  old_deleted = metadata->md_deleted;
  metadata->md_deleted = 0;

  /* Write the metafiles */
  if (write_metadata_file(metafile, metadata) ||
      write_default_file(dflfile, metadata->md_dfl_vid, metadata->md_dfl_svid))
    {
      /* Something failed, remove the version from memory */
      metadata->md_versions = version->v_next;
      metadata->md_dfl_vid = old_vid;
      metadata->md_dfl_svid = old_svid;
      metadata->md_deleted = old_deleted;

      free(metafile);
      free(dflfile);
      return -1;
    }

  free(metafile);
  free(dflfile);
  return 0;
}

#define TIME_LIMIT 1

/*
 * Create a new version or subversion of a file. This is a generic interface.
 * If subversion is set, it will create a subversion with the given attributes.
 * Else it will create a version. It won't work for initial version creation.
 */
static int create_new_version_generic(const char *vpath, int subversion,
				      int do_copy, mode_t mode, uid_t uid,
				      gid_t gid)
{
  metadata_t *metadata;
  version_t *version, *current;
  int result;

  /* We *want* to see deleted files there */
  rcs_ignore_deleted = 1;

  metadata = rcs_translate_to_metadata(vpath, rcs_version_path);
  assert(metadata != NULL);

  current = rcs_find_version(metadata, LATEST, LATEST);
  assert(current != NULL);

  /* Return to normal behavior */
  rcs_ignore_deleted = 0;

  /* Check timestamp in order not to create bogus new versions */
  if (time(NULL) - metadata->md_timestamp < TIME_LIMIT)
    return 0;

  /* Can't create a subversion from a deleted file */
  if (subversion && metadata->md_deleted)
    return -1;

  /* Create a new version in memory */
  version = safe_malloc(sizeof(version_t));
  if (subversion)
    {
      /* If we have a locked version, we have to bump the real version */
      if (current->v_vid != metadata->md_versions->v_vid)
	{
	  version->v_vid = metadata->md_versions->v_vid + 1;
	  version->v_svid = 0;
	}
      else
	{
	  version->v_vid = current->v_vid;
	  version->v_svid = current->v_svid + 1;
	}
      version->v_mode = mode & 07777;
      version->v_uid = uid;
      version->v_gid = gid;
      version->v_rfile = safe_strdup(current->v_rfile);
    }
  else
    {
      version->v_vid = metadata->md_versions->v_vid + 1;
      version->v_svid = 0;
      version->v_mode = current->v_mode & 07777;
      version->v_uid = do_copy ? current->v_uid : uid;
      version->v_gid = do_copy ? current->v_gid : gid;
      version->v_rfile = create_version_name(vpath, version->v_vid);
    }
  version->v_next = NULL;

  /* Create the file and copy the contents over, then link the version */
  if (!subversion && do_copy)
    result = create_copy_file(current->v_rfile, version->v_rfile);
  else
    result = 0;
  if (!result)
      result = create_link_version(metadata, version);
  if (result)
    {
      free(version->v_rfile);
      free(version);
    }

  return result;
}

/*
 * Create the metadata file for a new file
 */
static int create_new_metadata(const char *vpath, char *rpath, mode_t mode, uid_t uid, gid_t gid)
{
  metadata_t *metadata;
  version_t *version;
  char *metafile;
  int res;

  metadata = safe_malloc(sizeof (metadata_t));
  metadata->md_vfile = safe_strdup(vpath);
  metadata->md_vpath = helper_split_to_array(vpath, '/');
  metadata->md_deleted = 0;
  metadata->md_timestamp = time(NULL);
  metadata->md_dfl_vid = LATEST;
  metadata->md_dfl_svid = LATEST;
  version = safe_malloc(sizeof (version_t));
  metadata->md_versions = version;
  version->v_vid = 1;
  version->v_svid = 0;
  version->v_mode = mode;
  version->v_uid = uid;
  version->v_gid = gid;
  version->v_rfile = rpath;
  version->v_next = NULL;
  cache_add_metadata(metadata);
  metafile = create_meta_name(metadata->md_vfile, "metadata");
  res = write_metadata_file(metafile, metadata);
  free(metafile);
  return res;
}

/*
 * Create a new version of the file described by a virtual path. It handles
 * all the operations : copying the old version to a new version id, creating
 * the associated metadata and flushing everything to disk. It does *not*
 * handle the case where the file does not already exist !
 */
int create_new_version(const char *vpath)
{
  return create_new_version_generic(vpath, 0, 1, 0, 0, 0);
}

/*
 * Create a subversion of the file, using the specified new meta-information.
 * It needs a file with at least one version to work !
 */
int create_new_subversion(const char *vpath, mode_t mode, uid_t uid, gid_t gid)
{
  return create_new_version_generic(vpath, 1, 0, mode, uid, gid);
}

/*
 * Create a new empty file version 1.
 * Handle the case where an older version of the file aldready exists.
 */
int create_new_file(const char *vpath, mode_t mode, uid_t uid, gid_t gid, dev_t dev)
{
  metadata_t *metadata;
  mode_t create_mode;
  char *path;
  int res;

  if (!(mode && (S_IFREG | S_IFCHR | S_IFBLK | S_IFIFO | S_IFSOCK))) {
    return -EPERM;
  }
  /* check if an older version existed */
  rcs_ignore_deleted = 1;
  metadata = rcs_translate_to_metadata(vpath, rcs_version_path);
  rcs_ignore_deleted = 0;
  if (metadata && !metadata->md_deleted)
    return -EEXIST;
  if (!metadata)
    path = create_version_name(vpath, 1);
  else
    path = create_version_name(vpath, metadata->md_versions->v_vid + 1);

  /* remove suid, sgid, rwx for group and others */
  create_mode = (mode & ~S_ISUID & ~S_ISGID & ~S_ISVTX
		 & ~S_IRWXG & ~S_IRWXO) | S_IRWXU;
  if (mknod(path, create_mode, dev) == -1) {
    free(path);
    return -errno;
  }
  /* FIXME: the owned should be the same as the parent if suid */
  if (!metadata)
    res = create_new_metadata(vpath, path, mode & 07777, uid, gid);
  else
    res = create_new_version_generic(vpath, 0, 0, mode, uid, gid);

  /* Update timestamp */
  metadata = rcs_translate_to_metadata(vpath, rcs_version_path);
  if (metadata)
    metadata->md_timestamp = time(NULL);

  return res;
}

/*
 * Create a new symlink, version 1.
 * Handle the case where an older version of the file aldready exists.
 */
int create_new_symlink(const char *dest, const char *vpath, uid_t uid, gid_t gid)
{
  char *realpath;
  metadata_t *metadata;
  int res;

  /* check if an older version existed */
  rcs_ignore_deleted = 1;
  metadata = rcs_translate_to_metadata(vpath, rcs_version_path);
  rcs_ignore_deleted = 0;
  if (metadata && !metadata->md_deleted)
    return -EEXIST;
  if (!metadata)
    realpath = create_version_name(vpath, 1);
  else
    realpath = create_version_name(vpath, metadata->md_versions->v_vid + 1);
  if (symlink(dest, realpath) == -1) {
    free(realpath);
    return -errno;
  }
  /* FIXME: the owned should be the same as the parent if suid */
  if (!metadata)
    res = create_new_metadata(vpath, realpath, S_IRWXU | S_IRWXG | S_IRWXO, uid, gid);
  else
    res = create_new_version_generic(vpath, 0, 0, S_IRWXU | S_IRWXG | S_IRWXO, uid, gid);
  return res;
}

int create_new_directory(const char *vpath, mode_t mode, uid_t uid, gid_t gid)
{
  char *realpath;
  metadata_t *metadata;
  int res;

  /* check if an older version existed */
  rcs_ignore_deleted = 1;
  metadata = rcs_translate_to_metadata(vpath, rcs_version_path);
  rcs_ignore_deleted = 0;
  if (metadata && !metadata->md_deleted)
    return -EEXIST;
  if (!metadata)
    realpath = create_version_name(vpath, 1);
  else
    realpath = create_version_name(vpath, metadata->md_versions->v_vid + 1);
  /* FIXME: */
  if (mkdir(realpath, 0700) == -1) {
    free(realpath);
    return -errno;
  }
  /* FIXME: the owned should be the same as the parent if suid */
  if (!metadata)
    res = create_new_metadata(vpath, realpath, mode, uid, gid);
  else
    res = create_new_version_generic(vpath, 0, 0, mode, uid, gid);
  return res;
}

/*
 * Copy a (real) file to another (real) file.
 * file can be a regular file or a simlink
 */
int create_copy_file(const char *source, const char *target)
{
  struct stat src_stat;

  if (lstat(source, &src_stat) == -1)
    return -1;

  if (S_ISLNK(src_stat.st_mode)) {
    char lnk[1024];
    int lnk_size;
    if ((lnk_size = readlink(source, lnk, 1023)) == -1)
      return -2;
    lnk[lnk_size] = '\0';
    if (symlink(lnk, target) == -1)
      return -3;
  } else if (S_ISREG(src_stat.st_mode)) {
    int src, dst;
    int rsize;
    char buf[1024];
    if ((src = open(source, O_RDONLY)) == -1) {
      close(dst);
      return -4;
    }
    if ((dst = creat(target, src_stat.st_mode)) == -1)
      return -5;
    while ((rsize = read(src, buf, 1024))) {
      if (rsize == -1 && errno == EINTR)
	continue ;
      if (rsize == -1) {
	close(src);
	close(dst);
	return -6;
      }
      while (write(dst, buf, rsize) == -1)
	if (errno != EINTR) {
	  close(src);
	  close(dst);
	  return -7;
	}
    }
    close(src);
    close(dst);
  } else {
    return -8;
  }
  return 0;
}
