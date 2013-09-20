#ifndef EA_H
# define EA_H

int	callback_setxattr(const char *path, const char *name,
			  const char *value, size_t size, int flags);
int	callback_getxattr(const char *path, const char *name, char *value,
			  size_t size);
int	callback_listxattr(const char *path, char *list, size_t size);
int	callback_removexattr(const char *path, const char *name);

#endif /* !EA_H */
