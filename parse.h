#ifndef PARSE_H
# define PARSE_H

# include "structs.h"

metadata_t	*parse_metadata_file(char *metafile);
void		parse_default_file(char *dflfile, int *vid, int *svid);

metadata_t	*parse_metadata_for_file(char *root, char *filename);

#endif /* !PARSE_H */
