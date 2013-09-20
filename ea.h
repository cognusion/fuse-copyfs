/*
 * copyfs - copy on write filesystem  http://n0x.org/copyfs/
 * Copyright (C) 2004 Nicolas Vigier <boklm@mars-attacks.org>
 *                    Thomas Joubert <widan@net-42.eu.org>
 * This program can be distributed under the terms of the GNU GPL.
 * See the file COPYING.
*/

#ifndef EA_H
# define EA_H

int	callback_setxattr(const char *path, const char *name,
			  const char *value, size_t size, int flags);
int	callback_getxattr(const char *path, const char *name, char *value,
			  size_t size);
int	callback_listxattr(const char *path, char *list, size_t size);
int	callback_removexattr(const char *path, const char *name);

#endif /* !EA_H */
