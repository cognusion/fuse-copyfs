/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <assert.h>
#include <stdio.h>

#include "helper.h"
#include "rcs.h"


/*
 * Error-checking replacement for malloc.
 */
void *helper_malloc(size_t size, char *file, int line, const char *fn)
{
  void *result;

  result = malloc(size);
  if (!result)
    {
      fprintf(stderr, "Out of memory in malloc() at %s:%i [%s]\n", file, line,
	      fn);
      abort();
    }
  return result;
}

/*
 * Error-checking replacement for realloc.
 */
void *helper_realloc(void *ptr, size_t size, char *file, int line,
		     const char *fn)
{
  void *result;

  result = realloc(ptr, size);
  if (!result)
    {
      fprintf(stderr, "Out of memory in realloc() at %s:%i [%s]\n", file, line,
	      fn);
      abort();
    }
  return result;
}

/*
 * Error-checking replacement for strdup.
 */
char *helper_strdup(const char *str, char *file, int line, const char *fn)
{
  char *result;

  result = strdup(str);
  if (!result)
    {
      fprintf(stderr, "Out of memory in strdup() at %s:%i [%s]\n", file, line,
	      fn);
      abort();
    }
  return result;
}

/*
 * Split a character string into its constitutive elements, using a given
 * separator. Ignores empty elements (like repeats of the separator, or a
 * separator at the beginning or the end of the string) Returns a
 * NULL-terminated array.
 */
char **helper_split_to_array(const char *string, char separator)
{
  unsigned int count, i;
  char **result;
  int start;

  for (i = 0, count = 0; string[i]; i++)
    {
      /*
       * We have an item when :
       *  - we are at the beginning and the first character is not a separator
       *  - we are elsewhere and we are switching from a separator to another
       *    character
       */
      if (i == 0)
	{
	  if (string[i] != separator)
	    count++;
	}
      else if ((string[i - 1] == separator) && (string[i] != separator))
	count ++;
    }

  result = safe_malloc(sizeof(char *) * (count + 1));
  result[count] = NULL;

  /* Stop there if nothing to store */
  if (!count)
    return result;

  /* We look one behind, so we can catch the '\0' as a separator */
  for (count = 0, start = -1, i = 0; !i || string[i - 1]; i++)
    {
      if (!string[i] || (string[i] == separator))
	{
	  if (start == -1)
	    continue;
	  else
	    {
	      /* Save the item */
	      result[count] = safe_malloc(i - start + 1);
	      memcpy(result[count], string + start, i - start);
	      result[count][i - start] = '\0';
	      count++;
	      start = -1;
	    }
	}
      else if (start == -1)
	start = i;
    }

  return result;
}

/*
 * Free a string array.
 */
void helper_free_array(char **array)
{
  unsigned int i;

  for (i = 0; array[i]; i++)
    free(array[i]);
  free(array);
}

/*
 * Check if an array has a given prefix, ie the first array begins with
 * the items of the second, in order.
 */
int helper_array_has_prefix(char **longest, char **shortest)
{
  unsigned int i;

  for (i = 0; longest[i] && shortest[i]; i++)
    if (strcmp(longest[i], shortest[i]))
      return 0;
  return (shortest[i] == NULL);
}

/*
 * Concatenate strings and strings arrays with a given separator. The first
 * arguments tells the function what is expected at each position : 'S' is
 * a normal string, 'A' is an array, '-' means a fixed separator. No separator
 * is put at the beginning or end of the result by default.
 */
char *helper_build_composite(char *format, char *separator, ...)
{
  unsigned int length, i;
  va_list args;
  char *result;

  /* Count chars */
  va_start(args, separator);
  for (length = 0, i = 0; format[i]; i++)
    {
      assert((format[i] == 'S') || (format[i] == 'A') || (format[i] == '-'));

      if (format[i] == '-')
	length += strlen(separator);
      else
	{
	  if (format[i] == 'S')
	    length += strlen(va_arg(args, char *));
	  else
	    {
	      unsigned int j;
	      char **array;

	      array = va_arg(args, char **);
	      for (j = 0; array[j]; j++)
		{
		  length += strlen(array[j]);
		  if (array[j + 1])
		    length += strlen(separator);
		}
	    }

	  /* Don't add separators at the end, or before explicit separators */
	  if (format[i + 1] && (format[i + 1] != '-'))
	    length += strlen(separator);
	}
    }
  va_end(args);

  result = safe_malloc(length + 1);
  result[0] = '\0';

  /* Now do it */
  va_start(args, separator);
  for (i = 0; format[i]; i++)
    if (format[i] == '-')
      strcat(result, separator);
    else
      {
	if (format[i] == 'S')
	  strcat(result, va_arg(args, char *));
	else
	  {
	    unsigned int j;
	    char **array;

	    array = va_arg(args, char **);
	    for (j = 0; array[j]; j++)
	      {
		strcat(result, array[j]);
		if (array[j + 1])
		  strcat(result, separator);
	      }
	  }

	  if (format[i + 1] && (format[i + 1] != '-'))
	    strcat(result, separator);
      }

  /* All done */
  return result;
}

/*
 * Hash a string into an 8-bit number.
 */
unsigned char helper_hash_string(const char *string)
{
  unsigned char result;

  for (result = 0; *string; string++)
    result ^= *string;
  return result;
}

/*
 * Read a complete line from a file, that HAS to end with a '\n'. The
 * returned line does NOT contain the final '\n'.
 */
char *helper_read_line(FILE *fh)
{
  char *buffer = safe_malloc(LINE_BUFFER_STEP);
  int buffer_size = LINE_BUFFER_STEP;
  int buffer_pos = 0;
  int i, done = 0;

  do
    {
      if (!fgets(buffer + buffer_pos, buffer_size - buffer_pos, fh))
	{
	  free(buffer);
	  return NULL;
	}

      /* Check if we got whole line */
      for (i = buffer_pos; i < buffer_size; i++)
	if (buffer[i] == '\n')
	  {
	    buffer[i] = '\0';
	    done = 1;
	    break;
	  }

      if (!done)
	{
	  /* Not enough room yet */
	  buffer = safe_realloc(buffer, buffer_size + LINE_BUFFER_STEP);
	  buffer_pos = buffer_size - 1;
	  buffer_size += LINE_BUFFER_STEP;
	}
    }
  while (!done);

  return buffer;
}

/*
 * Get a complete prefixed file name, of the form <prefix>.<base>.
 */
char *helper_get_file_name(char *base, char *prefix)
{
  char *result;

  result = safe_malloc(strlen(base) + strlen(prefix) + 2);
  sprintf(result, "%s.%s", prefix, base);
  return result;
}

/*
 * Return the filename part of a path.
 */
char *helper_extract_filename(const char *path)
{
  char *result;

  result = rindex(path, '/');
  if (result)
    return safe_strdup(result + 1);
  else
    return safe_strdup(path);
}

/*
 * Return the dirname part of a path.
 */
char *helper_extract_dirname(const char *path)
{
  char *result, *position;

  result = safe_strdup(path);
  position = rindex(result, '/');
  if (position)
    *position = '\0';
  else
    *result = '\0';
  return safe_realloc(result, strlen(result) + 1);
}

/*
 * Build a metadata file name with the given virtual file and prefix.
 * prefix is "metadata" for metadata file and "dfl-meta" for default file.
 */
char *helper_create_meta_name(const char *vpath, char *prefix)
{
  char *dir, *file, *xlat, *name, *res;

  dir = helper_extract_dirname(vpath);
  file = helper_extract_filename(vpath);
  xlat = rcs_translate_path(dir, rcs_version_path);
  name = helper_build_composite("SS", ".", prefix, file);
  res = helper_build_composite("SS", "/", xlat, name);
  free(xlat);
  free(name);
  free(file);
  free(dir);
  return res;
}
