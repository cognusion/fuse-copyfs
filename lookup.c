/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "helper.h"
#include "structs.h"
#include "parse.h"
#include "cache.h"
#include "rcs.h"


/* Ignore delete flags */
int rcs_ignore_deleted = 0;


/*
 * Find a given version in a metadata set. If vid is LATEST, retrieves the
 * absolute latest version. If svid is LATEST and vid is not, retrieves the
 * latest subversion of that version, that is the latest revision of the
 * metadata.
 */
version_t *rcs_find_version(metadata_t *metadata, int vid, int svid)
{
  version_t *version;

  version = metadata->md_versions;

  /* Latest version is first, else find version block */
  if (vid == LATEST)
    {
      /*
       * If the file is marked as deleted, make it not appear. It should not
       * matter for callers. If the file was deleted last, just create a new
       * version, as if it never existed (except the version number will be
       * higher than 1)
       */
      if (metadata->md_deleted && !rcs_ignore_deleted)
	return NULL;

      /*
       * Check if there is a pinned version. If there is, we have a normal
       * version search.
       */
      if (metadata->md_dfl_vid == LATEST)
	return version;
      else
	{
	  vid = metadata->md_dfl_vid;
	  svid = metadata->md_dfl_svid;
	}
    }
  while (version && (version->v_vid > (unsigned)vid))
    version = version->v_next;

  if (!version || (version->v_vid != (unsigned)vid))
    {
      /*
       * If a default version makes no sense anymore, just use the real
       * latest and ignore the default.
       */
      if (metadata->md_dfl_vid != LATEST)
	return metadata->md_versions;

      /* No such version */
      return NULL;
    }

  /* Lookup subversion (ie metadata revision) */
  if (svid == LATEST)
    return version;
  while (version && (version->v_vid == (unsigned)vid) &&
	 (version->v_svid > (unsigned)svid))
    version = version->v_next;

  if (!version || (version->v_vid != (unsigned)vid) ||
      (version->v_svid != (unsigned)svid))
    {
      /*
       * If the "default" version does not exist anymore, use the real
       * default (it should never happen anyway)
       */
      if (metadata->md_dfl_vid != LATEST)
	return metadata->md_versions;

      /* This subversion does not exist */
      return NULL;
    }

  return version;
}

/*
 * Add the specified path to the versionned files in a metadata block.
 */
static void rcs_fixup_metadata_paths(metadata_t *metadata, char *path)
{
  version_t *version;

  for (version = metadata->md_versions; version; version = version->v_next)
    {
      char *new;

      new = helper_build_composite("SS", "/", path, version->v_rfile);
      free(version->v_rfile);
      version->v_rfile = new;
    }
}

/*
 * Insert the given number of items from a path as a virtual file name
 * in the given metadata.
 */
static void rcs_fixup_metadata_vfile(metadata_t *metadata, char **elements,
				     unsigned int count)
{
  unsigned int i;

  /* Make a copy of the proper portion of the path elements */
  metadata->md_vpath = safe_malloc(sizeof(char *) * (count + 1));
  for (i = 0; i < count; i++)
    metadata->md_vpath[i] = safe_strdup(elements[i]);
  metadata->md_vpath[i] = NULL;

  /* Build the composite path */
  metadata->md_vfile = helper_build_composite("-A", "/", metadata->md_vpath);
}

/*
 * Translate a path in the virtual filesystem to the real path of the
 * versionned file, assuming a given version directory. This does the
 * translation level by level, at each time using the preferred version
 * of the file.
 */
char *rcs_translate_path(const char *virtual, char *vroot)
{
  char **elements, *path;
  unsigned int i;
  int base;
  metadata_t *metadata;
  version_t *version;

  /* The root directory is special */
  if (!strcmp(virtual, "/"))
    {
      version_t *last;
      char *metafile, *dflfile;

      metadata = cache_get_metadata(virtual);
      if (metadata)
	{
	  version = rcs_find_version(metadata, LATEST, LATEST);
	  return safe_strdup(version->v_rfile);
	}

      metafile = helper_build_composite("SS", "/", vroot, "metadata.");
      dflfile = helper_build_composite("SS", "/", vroot, "dfl-meta.");

      /* The root HAS to have metadata */
      metadata = parse_metadata_file(metafile);
      assert(metadata != NULL);
      last = rcs_find_version(metadata, LATEST, LATEST);
      assert(last != NULL);

      /* Parse the default version file */
      parse_default_file(dflfile, &metadata->md_dfl_vid,
			 &metadata->md_dfl_svid);

      free(metafile);
      free(dflfile);

      /* Complete the metadata */
      metadata->md_vpath = safe_malloc(sizeof(char *));
      metadata->md_vpath[0] = NULL;

      for (version = metadata->md_versions; version; version = version->v_next)
	{
	  free(version->v_rfile);
	  version->v_rfile = safe_strdup(vroot);
	}
      metadata->md_vfile = safe_strdup("/");
      cache_add_metadata(metadata);
      return safe_strdup(last->v_rfile);
    }

  /* Get path elements */
  elements = helper_split_to_array(virtual, '/');

  /* Try to get a starting point from the cache, the more advanced possible */
  base = cache_find_maximal_match(elements, &metadata);
  if (base > 0)
    {
      version = rcs_find_version(metadata, LATEST, LATEST);
      if (!version)
	{
	  helper_free_array(elements);
	  return NULL;
	}
      path = version->v_rfile;
    }
  else
    {
      path = vroot;
      base = 0;
    }

  /* Iterate from the root onwards */
  for (i = base; elements[i]; i++)
    {
      /* Retrieve our metadata and find the proper version */
      metadata = parse_metadata_for_file(path, elements[i]);
      if (!metadata)
	{
	  helper_free_array(elements);
	  return NULL;
	}
      version = rcs_find_version(metadata, LATEST, LATEST);
      if (!version)
	{
	  helper_free_array(elements);
	  rcs_free_metadata(metadata);
	  return NULL;
	}

      /* Fixup this metadata, and add it to the cache */
      rcs_fixup_metadata_paths(metadata, path);
      rcs_fixup_metadata_vfile(metadata, elements, i + 1);
      cache_add_metadata(metadata);

      /* Get the new path from there */
      path = version->v_rfile;
    }

  /* If we got there, we found it, so return it */
  helper_free_array(elements);
  return safe_strdup(path);
}

/*
 * Get the metadata structure associated with a virtual file.
 */
metadata_t *rcs_translate_to_metadata(const char *vfile, char *vroot)
{
  char *path;

  path = rcs_translate_path(vfile, vroot);
  if (!path)
    return NULL;
  free(path);

  /*
   * Lookup the metadata that got put in the cache. It can't have been removed
   * yet, since cache cleaning is only done on insertions.
   */
  return cache_get_metadata(vfile);
}
