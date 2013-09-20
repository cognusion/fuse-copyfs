/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <attr/xattr.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fuse.h>

#include "helper.h"
#include "structs.h"
#include "write.h"
#include "rcs.h"
#include "ea.h"


/*
 * We support extended attributes to allow user-space scripts to manipulate
 * the file system state, such as forcing a specific version to appear, ...
 *
 * The supported attributes are :
 *
 *  - rcs.locked_version : the current locked version for the file
 *  - rcs.metadata_dump  : a dump of the metadata, for scripts that need
 *                         to list the available versions.
 */

/*
 * Set the value of an extended attribute.
 */
int callback_setxattr(const char *path, const char *name, const char *value,
		      size_t size, int flags)
{
  metadata_t *metadata;
  version_t *version;

  metadata = rcs_translate_to_metadata(path, rcs_version_path);
  if (!metadata)
    return -ENOENT;

  if (!strcmp(name, "rcs.locked_version"))
    {
      struct fuse_context *context;
      unsigned int length;
      int vid, svid;
      char *dflfile, *local;

      /* Copy the value to NUL-terminate it */
      local = safe_malloc(size + 1);
      local[size] = '\0';
      memcpy(local, value, size);

      vid = 0; svid = 0;
      if ((sscanf(local, "%d.%d%n", &vid, &svid, &length) != 2) ||
	  (length != size))
	{
	  free(local);
	  return -EINVAL;
	}
      free(local);

      /* Check if we actually have that version (or a compatible version) */
      for (version = metadata->md_versions; version; version = version->v_next)
	{
	  if (vid == -1)
	    break;
	  if ((version->v_vid == (unsigned)vid) && (svid == -1))
	    break;
	  if ((version->v_vid == (unsigned)vid) &&
	      (version->v_svid == (unsigned)svid))
	    break;
	}
      if (!version)
	return -EINVAL;

      /*
       * Only allow a user to change the version if the new version has the
       * same owner as the one requesting the change, or if the user is root,
       * to prevent curious users from resurrecting versions with too lax
       * permissions.
       */
      context = fuse_get_context();
      if ((context->uid != 0) && (context->uid != version->v_uid))
	return -EACCES;

      /* Try to commit to disk */
      dflfile = helper_create_meta_name(path, "dfl-meta");
      if (write_default_file(dflfile, vid, svid) != 0)
	{
	  free(dflfile);
	  return -errno;
	}
      free(dflfile);

      /* If ok, change in RAM */
      metadata->md_dfl_vid = vid;
      metadata->md_dfl_svid = svid;

      return 0;
    }
  else if (!strcmp(name, "rcs.metadata_dump"))
    {
      /* This one is read-only */
      return -EPERM;
    }
  else
    {
      int res;

      /* Pass those through */
      version = rcs_find_version(metadata, LATEST, LATEST);
      if (!version)
	return -ENOENT;
      res = lsetxattr(version->v_rfile, name, value, size, flags);
      if (res == -1)
	return -errno;
      return 0;
    }
}

/*
 * Get the value of an extended attribute.
 */
int callback_getxattr(const char *path, const char *name, char *value,
		      size_t size)
{
  metadata_t *metadata;
  version_t *version;

  metadata = rcs_translate_to_metadata(path, rcs_version_path);
  if (!metadata)
    return -ENOENT;

  if (!strcmp(name, "rcs.locked_version"))
    {
      char buffer[64];
      int vid, svid;

      if (metadata->md_dfl_vid == -1)
	{
	  vid = metadata->md_versions->v_vid;
	  svid = metadata->md_versions->v_svid;
	}
      else
	{
	  vid = metadata->md_dfl_vid;
	  svid = metadata->md_dfl_svid;
	}

      /* Get the version number */
      snprintf(buffer, 64, "%i.%i", vid, svid);

      /* Handle the EA protocol */
      if (size == 0)
	return strlen(buffer);
      if (strlen(buffer) > size)
	return -ERANGE;
      strcpy(value, buffer);
      return strlen(buffer);
    }
  else if (!strcmp(name, "rcs.metadata_dump"))
    {
      char **array, *result;
      unsigned int count;
      int res;

      /*
       * We need to pass the version metadata to userspace, but we also need
       * to pass the file type and modification type from the stat syscall,
       * since the userspace program may be running as a non-root, and thus
       * can't see the version store.
       */

      for (count = 0, version = metadata->md_versions; version;
	   version = version->v_next)
	count++;
      array = safe_malloc(sizeof(char *) * (count + 1));
      memset(array, 0, sizeof(char *) * (count + 1));

      /* Traverse the version list and build the individual strings */
      for (count = 0, version = metadata->md_versions; version;
	   version = version->v_next)
	{
	  struct stat st_data;

	  /* stat() the real file, but just ignore failures (bad version ?) */
	  if (lstat(version->v_rfile, &st_data) < 0)
	    {
	      st_data.st_mode = S_IFREG;
	      st_data.st_mtime = -1;
	    }
	  else
	    st_data.st_mode &= ~07777;

	  if (asprintf(&array[count], "%d:%d:%d:%d:%d:%lld:%ld",
		       version->v_vid, version->v_svid,
		       version->v_mode | st_data.st_mode, version->v_uid,
		       version->v_gid, st_data.st_size, st_data.st_mtime) < 0)
	    {
	      unsigned int i;

	      /* Free everything if it failed */
	      for (i = 0; i < count; i++)
		free(array[i]);
	      free(array);
	      return -ENOMEM;
	    }

	  count++;
	}

      /* Build the final string */
      result = helper_build_composite("A", "|", array);
      helper_free_array(array);

      /* Handle the EA protocol */

      if (size == 0)
	res = strlen(result);
      else if (strlen(result) > size)
	res = -ERANGE;
      else
	{
	  strcpy(value, result);
	  res = strlen(result);
	}
      free(result);
      return res;
    }
  else
    {
      int res;

      /*
       * We are not interested in those, simply forward them to the real
       * filesystem.
       */
      version = rcs_find_version(metadata, LATEST, LATEST);
      if (!version)
	return -ENOENT;
      res = lgetxattr(version->v_rfile, name, value, size);
      if (res == -1)
	return -errno;
      return res;
    }
}

#define ATTRIBUTE_STRING "rcs.locked_version\0rcs.metadata_dump"

/*
 * List the supported extended attributes.
 */
int callback_listxattr(const char *path, char *list, size_t size)
{
  char *rpath, *buffer;
  unsigned int length;
  int res;

  rpath = rcs_translate_path(path, rcs_version_path);
  if (!rpath)
    return -ENOENT;

  /* We need to get the EAs of the real file, and mix our own */
  res = llistxattr(path, NULL, 0);
  if (res == -1)
    {
      /* Ignore errors, as many filesystems don't support EA */
      length = 0;
      buffer = safe_malloc(sizeof(ATTRIBUTE_STRING));
    }
  else
    {
      length = res;
      buffer = safe_malloc(length + sizeof(ATTRIBUTE_STRING));
      if (llistxattr(rpath, buffer, length) == -1)
	{
	  free(buffer);
	  free(rpath);
	  return -errno;
	}
    }

  free(rpath);

  /* Append ours to the buffer */
  memcpy(buffer + length, ATTRIBUTE_STRING, sizeof(ATTRIBUTE_STRING));
  length += sizeof(ATTRIBUTE_STRING);

  /* Handle the EA protocol */
  if (size == 0)
    res = length;
  else if (length > size)
    res = -ERANGE;
  else
    {
      memcpy(list, buffer, length);
      res = length;
    }
  free(buffer);
  return res;
}

/*
 * Remove an extended attribute.
 */
int callback_removexattr(const char *path, const char *name)
{
  if (!strcmp(name, "rcs.locked_version") ||
      !strcmp(name, "rcs.metadata_dump"))
    {
      /* Our attributes can't be deleted */
      return -EPERM;
    }
  else
    {
      char *rpath;
      int res;

      rpath = rcs_translate_path(path, rcs_version_path);
      res = lremovexattr(rpath, name);
      free(rpath);
      if (res == -1)
        return -errno;
      return 0;
    }
}
