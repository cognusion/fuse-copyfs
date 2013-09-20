/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#ifndef RCS_H
# define RCS_H

# include "structs.h"

extern char	*rcs_version_path;
extern int	rcs_ignore_deleted;

version_t	*rcs_find_version(metadata_t *metadata, int vid, int svid);
char		*rcs_translate_path(const char *virtual, char *vroot);
metadata_t	*rcs_translate_to_metadata(const char *vfile, char *vroot);

void		rcs_free_metadata(metadata_t *metadata);

#endif /* !RCS_H */
