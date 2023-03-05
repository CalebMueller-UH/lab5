/*
extensionFns.h
*/

#pragma once

#include <stdarg.h>
#include <stdio.h>

int snprintf_s(char *str, size_t strmax, const char *format, ...) {
  va_list args;
  int n;

  va_start(args, format);
  n = vsnprintf(str, strmax, format, args);
  va_end(args);

  if (n < 0 || n >= (int)strmax) {
    /* handle error */
    return -1;
  }

  return n;
}