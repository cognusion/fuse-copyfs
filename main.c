/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#include <sys/types.h>
#include <sys/stat.h>
#include <stdio.h>
#include <strings.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <fuse.h>

#include "helper.h"
#include "structs.h"
#include "cache.h"
#include "create.h"

char *rcs_version_path = "/home/widan/versions";

void rcs_free_metadata(metadata_t *metadata)
{
  version_t *version, *next;

  version = metadata->md_versions;
  while (version)
    {
      next = version->v_next;
      free(version->v_rfile);
      free(version);
      version = next;
    }
  if (metadata->md_vpath)
    helper_free_array(metadata->md_vpath);
  free(metadata->md_vfile);
  free(metadata);
}

#if 0
void set_config()
{
  gl_config = getenv("CPYFS_MIRROR");
  if (gl_config.mirrored_dir == NULL) {
    gl_config.mirrored_dir = "/var";
  }
  gl_config.backup_dir = getenv("CPYFS_BACKDIR");
  if (gl_config.backup_dir == NULL) {
    gl_config.backup_dir = "/tmp";
  }
}

int main(int argc, char *argv[])
{
    fuse_main(argc, argv, &callback_oper);
    return 0;
}
#endif


extern struct fuse_operations callback_oper;

int main(int argc, char **argv)
{
  rcs_version_path = getenv("RCS_VERSION_PATH");
  if (!rcs_version_path)
    {
      fprintf(stderr, "RCS_VERSION_PATH not defined in environment.\n");
      fprintf(stderr, "You really should use he `copyfs-mount' script.\n");
      exit(1);
    }

  /* Restrict permissions on create files */
  umask(0077);

  cache_initialize();
  fuse_main(argc, argv, &callback_oper);
  cache_finalize();
  exit(0);
}
