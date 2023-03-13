/*
color.h
*/

#pragma once

#include "constants.h"

typedef enum {
  RED,
  BOLD_RED,
  ORANGE,
  BOLD_ORANGE,
  YELLOW,
  BOLD_YELLOW,
  GREEN,
  BOLD_GREEN,
  CYAN,
  BOLD_CYAN,
  BLUE,
  BOLD_BLUE,
  MAGENTA,
  BOLD_MAGENTA,
  PURPLE,
  BOLD_PURPLE,
  GREY,
  BOLD_GREY
} color;

void colorPrint(color c, const char *format, ...);

// Forward declaration of size_t
typedef unsigned long size_t;

int colorSnprintf(char *str, size_t size, color c, const char *format, ...);