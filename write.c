/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#include <stdio.h>
#include <strings.h>
#include <unistd.h>
#include <errno.h>

#include "helper.h"
#include "structs.h"
#include "write.h"


/*
 * Worker function for version writing.
 */
static int write_metadata_file_worker(FILE *fh, version_t *version)
{
  char *name;

  /* Write them in order */
  if (version->v_next)
    if (write_metadata_file_worker(fh, version->v_next) != 0)
      return -1;

  /* Strip the path */
  name = rindex(version->v_rfile, '/');
  if (!name)
    name = version->v_rfile;
  else
    name++;

  /* Write this version's information */
  if (fprintf(fh, "%i:%i:%04o:%i:%i:%s\n", version->v_vid, version->v_svid,
	      version->v_mode, version->v_uid, version->v_gid, name) < 0)
    return -1;
  return 0;
}

/*
 * Write a metadata file on disk from the in-memory structures. We rewrite
 * the whole file to avoid leaving "deletion lines" at all the points the
 * file was deleted and re-created. Plus it cleans up partially corrupt files
 * automatically. Returns non-zero on error.
 */
int write_metadata_file(char *metafile, metadata_t *metadata)
{
  FILE *fh;

  /* Open metadata file */
  fh = fopen(metafile, "w");
  if (!fh)
    return -1;

  /* Write normal versions */
  if (write_metadata_file_worker(fh, metadata->md_versions) != 0)
    {
      fclose(fh);
      return -1;
    }

  /* If the file is marked deleted, put a killer version */
  if (metadata->md_deleted)
    if (fprintf(fh, "0:0:0000:0:0:\n") < 0)
      {
	fclose(fh);
	return -1;
      }

  fclose(fh);
  return 0;
}

/*
 * Write a default version file for the given version. Return non-zero on
 * error.
 */
int write_default_file(char *dflfile, int vid, int svid)
{
  FILE *fh;

  /* If we want to return it to default, remove the file */
  if (vid == LATEST)
    {
      int result;

      result = unlink(dflfile);
      if (!result || (errno == ENOENT))
	return 0;
      else
	return result;
    }

  fh = fopen(dflfile, "w");
  if (!fh)
    return -1;

  /* Write new value */
  if ((fprintf(fh, "%i.%i\n", vid, svid) < 0))
    {
      fclose(fh);
      return -1;
    }

  fclose(fh);
  return 0;
}
