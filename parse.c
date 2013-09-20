/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#include <sys/time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "helper.h"
#include "structs.h"


/*
 * Parse a line of the metadata file into a version data structure.
 */
static version_t *parse_version_line(FILE *fh)
{
  version_t *v_info;
  char *buffer;
  unsigned int l_vid, l_svid, l_mode, l_uid, l_gid;
  int name_pos;

  buffer = helper_read_line(fh);
  if (!buffer)
    return NULL;

  /* Parse the line */
  if (sscanf(buffer, "%u:%u:%o:%u:%u:%n", &l_vid, &l_svid, &l_mode, &l_uid,
	     &l_gid, &name_pos) != 5)
    {
      /*
       * Metadata has been corrupt. Someone tried to modify it manually and
       * screwed it up. Simply ignore it, and try to recover on the next
       * valid metadata.
       */
      free(buffer);
      return NULL;
    }
  else
    {
      v_info = safe_malloc(sizeof(version_t));
      v_info->v_vid = l_vid;
      v_info->v_svid = l_svid;
      v_info->v_mode = (mode_t)l_mode;
      v_info->v_uid = (uid_t)l_uid;
      v_info->v_gid = (gid_t)l_gid;

      /*
       * Don't try to append a path just now, since we may need those
       * metadata for the purpose of path translation. This will be done
       * later.
       */
      v_info->v_rfile = safe_strdup(buffer + name_pos);

      free(buffer);
      return v_info;
    }
}

/*
 * Parse a complete metadata file into the equivalent memory structure,
 * but do not try to resolve paths to the actual versionned files.
 * The returned metadata lists all versions, but cannot be used as is : it
 * is still necessary to resolve all paths, put in the name of the virtual
 * file, put in the default version number...
 */
metadata_t *parse_metadata_file(char *metafile)
{
  FILE *fh;
  metadata_t *md_info;
  version_t *v_info;
  int deleted;

  fh = fopen(metafile, "r");
  if (!fh)
    {
      /* No metadata, maybe we do not know the file yet */
      return NULL;
    }

  md_info = safe_malloc(sizeof(metadata_t));

  /* We do not know the virtual path here, it is up to the caller to set it */
  md_info->md_vfile = NULL;
  md_info->md_vpath = NULL;
  md_info->md_versions = NULL;

  /* Parse it line per line and link the data */
  deleted = 0;
  do
    {
      v_info = parse_version_line(fh);
      if (v_info)
	{
	  /*
	   * The "zero-version" means the file has been deleted. It is not
	   * actually used, it just flags deletion, so don't put it in the
	   * list.
	   */
	  if (!v_info->v_vid)
	    {
	      deleted = 1;
	      free(v_info->v_rfile);
	      free(v_info);
	    }
	  else
	    {
	      /* Remove the deleted flag */
	      deleted = 0;

	      /*
	       * Link it in decreasing order of versions. It depends on the
	       * metadata file being ordered from first to last version (ie
	       * normal reading order) !
	       */
	      v_info->v_next = md_info->md_versions;
	      md_info->md_versions = v_info;
	    }
	}
    }
  while (!feof(fh));

  /* Tag the file as deleted or not */
  md_info->md_deleted = deleted;

  /* Never touched */
  md_info->md_timestamp = 0;

  /* Default version is latest (it will be replaced later if needed) */
  md_info->md_dfl_vid = LATEST;
  md_info->md_dfl_svid = LATEST;

  /* Initialize the "control" data */
  md_info->md_next = NULL;
  md_info->md_previous = NULL;

  fclose(fh);
  return md_info;
}

/*
 * Parse a default version file and extract the preferred version.
 */
void parse_default_file(char *dflfile, int *vid, int *svid)
{
  char *line;
  FILE *fh;

  fh = fopen(dflfile, "r");
  if (!fh)
    {
      /* No default, assume user wants real latest */
      *vid = LATEST;
      *svid = LATEST;
      return;
    }

  line = helper_read_line(fh);
  if (sscanf(line, "%i.%i", vid, svid) != 2)
    {
      /* The file is probably corrupt, just assume real latest version */
      *vid = LATEST;
      *svid = LATEST;
    }

  free(line);
  fclose(fh);
}

/*
 * Retrieve metadata in a given root for a given file. It does not resolve
 * paths yet, but gets the preferred versions.
 */
metadata_t *parse_metadata_for_file(char *root, char *filename)
{
  char *metafile, *dflfile, *metapath, *dflpath;
  metadata_t *metadata;

  /* Build the path for the metadata file */
  metafile = helper_get_file_name(filename, "metadata");
  metapath = helper_build_composite("SS", "/", root, metafile);
  free(metafile);

  /* Read the partial metadata */
  metadata = parse_metadata_file(metapath);
  if (!metadata)
    {
      /* No metadata, we don't know about this file yet */
      free(metapath);
      return NULL;
    }

  /* Build the path for the default version file */
  dflfile = helper_get_file_name(filename, "dfl-meta");
  dflpath = helper_build_composite("SS", "/", root, dflfile);
  free(dflfile);

  /* Parse the default version file */
  parse_default_file(dflpath, &metadata->md_dfl_vid, &metadata->md_dfl_svid);

  free(metapath);
  free(dflpath);
  return metadata;
}
