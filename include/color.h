/*
color.h
*/

#pragma once

#include <stdarg.h>
#include <stdio.h>

enum color {
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
};

void colorPrint(enum color c, const char *format, ...);