/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#ifndef HELPER_H
# define HELPER_H

# include <stdio.h>
# include <sys/types.h>

# define LINE_BUFFER_STEP	1024


/*
 * Safe memory allocation
 */

void		*helper_malloc(size_t size, char *file, int line,
			       const char *fn);
void		*helper_realloc(void *ptr, size_t size, char *file, int line,
				const char *fn);
char		*helper_strdup(const char *str, char *file, int line,
			       const char *fn);

# define safe_malloc(s)							\
    helper_malloc(s, __FILE__, __LINE__, __func__)
# define safe_realloc(p, s)						\
    helper_realloc(p, s, __FILE__, __LINE__, __func__)
# define safe_strdup(s)							\
    helper_strdup(s, __FILE__, __LINE__, __func__)


/*
 * String arrays
 */

char		**helper_split_to_array(const char *string, char separator);
void		helper_free_array(char **array);
int		helper_array_has_prefix(char **longest, char **shortest);
char		*helper_build_composite(char *format, char *separator, ...);


/*
 * Miscellaneous things
 */

unsigned char	helper_hash_string(const char *string);
char		*helper_read_line(FILE *fh);
char		*helper_get_file_name(char *base, char *prefix);
char		*helper_extract_filename(const char *path);
char		*helper_extract_dirname(const char *path);
char		*helper_create_meta_name(const char *vpath, char *prefix);

#endif /* !HELPER_H */
