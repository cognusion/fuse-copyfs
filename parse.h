/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#ifndef PARSE_H
# define PARSE_H

# include "structs.h"

metadata_t	*parse_metadata_file(char *metafile);
void		parse_default_file(char *dflfile, int *vid, int *svid);

metadata_t	*parse_metadata_for_file(char *root, char *filename);

#endif /* !PARSE_H */
