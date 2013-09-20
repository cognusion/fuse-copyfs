/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#ifndef STRUCTS_H
# define STRUCTS_H

# include <sys/types.h>
# include <sys/time.h>

# define LATEST			-1

/* Data types */

typedef struct version_t	version_t;
typedef struct metadata_t	metadata_t;
typedef struct bucket_t		bucket_t;

struct				version_t
{
  unsigned int			v_vid;		/* Version ID		*/
  unsigned int			v_svid;		/* Subversion ID	*/

  mode_t			v_mode;		/* File permissions	*/
  uid_t				v_uid;		/* Owner		*/
  gid_t				v_gid;		/* Group		*/

  char				*v_rfile;	/* Real file name	*/

  version_t			*v_next;	/* Next version		*/
};

struct				metadata_t
{
  char				*md_vfile;	/* Virtual file name	*/
  char				**md_vpath;	/* Virtual path		*/
  version_t			*md_versions;	/* List of versions	*/
  int				md_deleted;	/* File deleted ?	*/
  int				md_dfl_vid;	/* Default version	*/
  int				md_dfl_svid;	/* Default subversion	*/
  time_t			md_timestamp;	/* Mod. begin		*/

  metadata_t			*md_next;	/* Next file in bucket	*/
  metadata_t			*md_previous;	/* Previous "		*/
};

struct				bucket_t
{
  unsigned int			b_count;	/* Item count		*/
  metadata_t			*b_contents;	/* Metadata chain	*/
};

#endif /* !STRUCTS_H */
