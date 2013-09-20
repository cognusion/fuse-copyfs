/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#ifndef CACHE_H
# define CACHE_H

# include "structs.h"

# define CACHE_SIZE 256 /* low for testing */
# define CACHE_HASH_BUCKETS 128

# define CACHE_HASH(x) (helper_hash_string((x)) % (CACHE_HASH_BUCKETS))

void		cache_initialize(void);
void		cache_finalize(void);
metadata_t	*cache_get_metadata(const char *vpath);
void		cache_add_metadata(metadata_t *metadata);
int		cache_find_maximal_match(char **array, metadata_t **result);

#endif /* !CACHE_H */
