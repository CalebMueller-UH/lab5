/*
filebuf.c
*/

#include "filebuf.h"

/* Initialize file buffer data structure */
void file_buf_init(struct File_buf *f) {
  f->head = 0;
  f->tail = HOST_MAX_FILE_BUFFER;
  f->occ = 0;
  f->name_length = 0;
}

/* Get the file name in the file buffer and store it in name
   Terminate the string in name with the null character
 */
void file_buf_get_name(struct File_buf *f, char name[]) {
  strncpy(name, f->name, f->name_length);
  name[f->name_length] = '\0';
}

void file_buf_put_name(struct File_buf *f, char *name, int length) {
  strncpy(f->name, name, length);
  f->name_length = length;
}

/*
 *  Add 'length' bytes n string[] to the file buffer
 */
int file_buf_add(struct File_buf *f, char string[], int length) {
  int i = 0;

  while (i < length && f->occ < HOST_MAX_FILE_BUFFER) {
    f->tail = (f->tail + 1) % (HOST_MAX_FILE_BUFFER + 1);
    f->buffer[f->tail] = string[i];
    i++;
    f->occ++;
  }
  return (i);
}

/*
 *  Remove bytes from the file buffer and store it in string[]
 *  The number of bytes is length.
 */
int file_buf_remove(struct File_buf *f, char string[], int length) {
  int i = 0;

  while (i < length && f->occ > 0) {
    string[i] = f->buffer[f->head];
    f->head = (f->head + 1) % (HOST_MAX_FILE_BUFFER + 1);
    i++;
    f->occ--;
  }

  return (i);
}