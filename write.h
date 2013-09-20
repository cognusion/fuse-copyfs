/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#ifndef WRITE_H
# define WRITE_H

int write_metadata_file(char *metafile, metadata_t *metadata);
int write_default_file(char *dflfile, int vid, int svid);

#endif /* !WRITE_H */
