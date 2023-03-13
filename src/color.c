/*
color.c
*/

#include "color.h"

#include <stdarg.h>
#include <stdio.h>

const int NUM_COLORS = BOLD_GREY + 1;

void colorPrint(color c, const char *format, ...) {
  va_list args;
  va_start(args, format);

  switch (c) {
    case RED:
      printf("\033[0;31m");
      break;
    case BOLD_RED:
      printf("\033[1;31m");
      break;
    case ORANGE:
      printf("\033[0;33m");
      break;
    case BOLD_ORANGE:
      printf("\033[1;38;5;208m");
      break;
    case YELLOW:
      printf("\033[0;33m");
      break;
    case BOLD_YELLOW:
      printf("\033[1;33m");
      break;
    case GREEN:
      printf("\033[0;32m");
      break;
    case BOLD_GREEN:
      printf("\033[1;32m");
      break;
    case CYAN:
      printf("\033[0;36m");
      break;
    case BOLD_CYAN:
      printf("\033[1;36m");
      break;
    case BLUE:
      printf("\033[0;34m");
      break;
    case BOLD_BLUE:
      printf("\033[1;34m");
      break;
    case MAGENTA:
      printf("\033[0;35m");
      break;
    case BOLD_MAGENTA:
      printf("\033[1;35m");
      break;
    case PURPLE:
      printf("\033[0;35m");
      break;
    case BOLD_PURPLE:
      printf("\033[1;35m");
      break;
    case GREY:
      printf("\033[0;90m");
      break;
    case BOLD_GREY:
      printf("\033[1;90m");
      break;
    default:
      break;
  }
  vprintf(format, args);
  printf("\033[0m");
  va_end(args);
}

int colorSnprintf(char *str, size_t size, color c, const char *format, ...) {
  va_list args;
  va_start(args, format);

  int written = 0;
  switch (c) {
    case RED:
      written += snprintf(str, size, "\033[0;31m");
      break;
    case BOLD_RED:
      written += snprintf(str, size, "\033[1;31m");
      break;
    case ORANGE:
      written += snprintf(str, size, "\033[0;33m");
      break;
    case BOLD_ORANGE:
      written += snprintf(str, size, "\033[1;38;5;208m");
      break;
    case YELLOW:
      written += snprintf(str, size, "\033[0;33m");
      break;
    case BOLD_YELLOW:
      written += snprintf(str, size, "\033[1;33m");
      break;
    case GREEN:
      written += snprintf(str, size, "\033[0;32m");
      break;
    case BOLD_GREEN:
      written += snprintf(str, size, "\033[1;32m");
      break;
    case CYAN:
      written += snprintf(str, size, "\033[0;36m");
      break;
    case BOLD_CYAN:
      written += snprintf(str, size, "\033[1;36m");
      break;
    case BLUE:
      written += snprintf(str, size, "\033[0;34m");
      break;
    case BOLD_BLUE:
      written += snprintf(str, size, "\033[1;34m");
      break;
    case MAGENTA:
      written += snprintf(str, size, "\033[0;35m");
      break;
    case BOLD_MAGENTA:
      written += snprintf(str, size, "\033[1;35m");
      break;
    case PURPLE:
      written += snprintf(str, size, "\033[0;35m");
      break;
    case BOLD_PURPLE:
      written += snprintf(str, size, "\033[1;35m");
      break;
    case GREY:
      written += snprintf(str, size, "\033[0;90m");
      break;
    case BOLD_GREY:
      written += snprintf(str, size, "\033[1;90m");
      break;
    default:
      break;
  }

  written += vsnprintf(str + written, size - written, format, args);

  written += snprintf(str + written, size - written, "\033[0m");

  va_end(args);

  return written;
}