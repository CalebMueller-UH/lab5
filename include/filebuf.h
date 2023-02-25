/*
filebuf.h
*/

#pragma once

#include <stdio.h>
#include <string.h>

#include "constants.h"

#define HOST_MAX_FILE_BUFFER 1000

struct File_buf {
  char name[MAX_FILENAME_LENGTH];
  int name_length;
  char buffer[HOST_MAX_FILE_BUFFER + 1];
  int head;
  int tail;
  int occ;
  FILE *fd;
};

/* Initialize file buffer data structure */
void file_buf_init(struct File_buf *f);

/* Get the file name in the file buffer and store it in name
   Terminate the string in name with the null character
 */
void file_buf_get_name(struct File_buf *f, char name[]);

/*
 *  Add 'length' bytes n string[] to the file buffer
 */
int file_buf_add(struct File_buf *f, char string[], int length);

/*
 *  Remove bytes from the file buffer and store it in string[]
 *  The number of bytes is length.
 */
int file_buf_remove(struct File_buf *f, char string[], int length);