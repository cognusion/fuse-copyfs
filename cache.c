/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#include <sys/time.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "helper.h"
#include "structs.h"
#include "cache.h"
#include "rcs.h"

static bucket_t cache_hash_table[CACHE_HASH_BUCKETS];
static unsigned int cache_item_count = 0;


/*
 * Initialize the cache control structures.
 */
void cache_initialize(void)
{
  unsigned int i;

  /* Initialize the buckets */
  for (i = 0; i < CACHE_HASH_BUCKETS; i++)
    {
      cache_hash_table[i].b_count = 0;
      cache_hash_table[i].b_contents = NULL;
    }

  /* No items in there */
  cache_item_count = 0;
}

/*
 * Free the cache data.
 */
void cache_finalize(void)
{
  unsigned int i;

  for (i = 0; i < CACHE_HASH_BUCKETS; i++)
    {
      metadata_t *metadata, *next;

      metadata = cache_hash_table[i].b_contents;
      while (metadata)
	{
	  next = metadata->md_next;
	  rcs_free_metadata(metadata);
	  metadata = next;
	}
      cache_hash_table[i].b_count = 0;
      cache_hash_table[i].b_contents = NULL;
    }
}

/*
 * Retrieve file metadata from the cache, assuming there is some in there.
 * It returns NULL if there is no *cached* metadata. It is the caller's
 * responsibility to try to read the metadata on disk (and if possible feed
 * it to the cache too)
 *
 * Items accessed are bumped to the top of their hash bucket, so repeat
 * accesses are faster, and so these items don't get zapped when the cache
 * grows too much and it is necessary to clean it up.
 */
metadata_t *cache_get_metadata(const char *vpath)
{
  bucket_t *bucket;
  metadata_t *metadata;

  /* Lookup the item */
  bucket = &cache_hash_table[CACHE_HASH(vpath)];
  metadata = bucket->b_contents;
  while (metadata && strcmp(metadata->md_vfile, vpath))
    metadata = metadata->md_next;
  if (!metadata)
    return NULL;

  /* Disconnect it from the list, if we are not at the beginning */
  if (metadata->md_previous)
    {
      metadata->md_previous->md_next = metadata->md_next;
      if (metadata->md_next)
	metadata->md_next->md_previous = metadata->md_previous;

      /* Reconnect it on top */
      metadata->md_previous = NULL;
      metadata->md_next = bucket->b_contents;
      bucket->b_contents->md_previous = NULL;
      bucket->b_contents = metadata;
    }

  return metadata;
}

/*
 * Clean the older items out of the cache to free space. The goal is to
 * half the number of items, so that we don't get called constantly.
 */
void cache_cleanup_old_items()
{
  /* FIXME: implement it if it really needed */
}

/*
 * Insert file metadata into the cache. It does not check if the data is
 * already there, so try to avoid putting it twice (especially if the two
 * instances are different !)
 *
 * If the cache has grown too big, it is cleaned up, the least recently
 * used items going away. That way we avoid to trash frequently-used entries.
 */
void cache_add_metadata(metadata_t *metadata)
{
  bucket_t *bucket;

  /* Check if cache needs cleaning */
  if (cache_item_count == CACHE_SIZE)
    cache_cleanup_old_items();

  /* Insert the element */
  bucket = &cache_hash_table[CACHE_HASH(metadata->md_vfile)];
  metadata->md_previous = NULL;
  metadata->md_next = bucket->b_contents;
  if (bucket->b_contents)
    bucket->b_contents->md_previous = metadata;
  bucket->b_contents = metadata;

  /* Bump the bucket's item counter, and the global counter */
  cache_item_count++;
  bucket->b_count++;
}

/*
 * Try to find the path composed of the maximal number of elements of the
 * array that will have a cache hit. Return the count, and the metadata. It
 * returns a count of -1 if there is no hit at all.
 */
int cache_find_maximal_match(char **array, metadata_t **result)
{
  int count;

  /* Count items */
  for (count = 0; array[count]; count++) ;

  for (/* Nothing */; count >= 0; count--)
    {
      char *old_value, *path;

      /* Try with this count */
      old_value = array[count];
      array[count] = NULL;
      path = helper_build_composite("-A", "/", array);
      array[count] = old_value;
      *result = cache_get_metadata(path);
      free(path);
      if (*result)
	return count;
    }

  return -1;
}
