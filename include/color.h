/*
color.h
*/

#pragma once

#include <stdarg.h>
#include <stdio.h>

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

extern const int NUM_COLORS;

void colorPrint(color c, const char *format, ...);