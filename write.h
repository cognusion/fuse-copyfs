#ifndef WRITE_H
# define WRITE_H

int write_metadata_file(char *metafile, metadata_t *metadata);
int write_default_file(char *dflfile, int vid, int svid);

#endif /* !WRITE_H */
