/**
 * Attribution to sudo.ws sources as a inspiration for this:
 *
 * Copyright (c) 2013-2014 Todd C. Miller <Todd.Miller@sudo.ws>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include "utils_strtonum.h"

#include <ctype.h>
#include <errno.h>
#include <string.h>

enum strtonum_err { STN_VALID, STN_INVALID, STN_TOOSMALL, STN_TOOBIG };

/*
 * Convert a string to a number in the range [minval, maxval]
 */
long long strtonum(const char *str, long long minval, long long maxval,
                   const char **errstrp) {
  const unsigned char *ustr = (const unsigned char *)str;
  enum strtonum_err errval = STN_VALID;
  long long lastval, result = 0;
  unsigned char dig, sign;
  int remainder;

  if (minval > maxval) {
    errval = STN_INVALID;
    goto done;
  }

  /* Trim leading space and check sign, if any. */
  while (isspace(*ustr)) {
    ustr++;
  }
  switch (*ustr) {
  case '-':
    sign = '-';
    ustr++;
    break;
  case '+':
    ustr++;
    /* FALLTHROUGH */
  default:
    sign = '+';
    break;
  }

  /*
   * To prevent overflow we determine the highest (or lowest in
   * the case of negative numbers) value result can have *before*
   * if its multiplied (divided) by 10 as well as the remainder.
   * If result matches this value and the next digit is larger than
   * the remainder, we know the result is out of range.
   * The remainder is always positive since it is compared against
   * an unsigned digit.
   */
  if (sign == '-') {
    lastval = minval / 10;
    remainder = -(minval % 10);
    if (remainder < 0) {
      lastval += 1;
      remainder += 10;
    }
    while ((dig = *ustr++) != '\0') {
      if (!isdigit(dig)) {
        errval = STN_INVALID;
        break;
      }
      dig -= '0';
      if (result < lastval || (result == lastval && dig > remainder)) {
        errval = STN_TOOSMALL;
        break;
      } else {
        result *= 10;
        result -= dig;
      }
    }
    if (result > maxval)
      errval = STN_TOOBIG;
  } else {
    lastval = maxval / 10;
    remainder = maxval % 10;
    while ((dig = *ustr++) != '\0') {
      if (!isdigit(dig)) {
        errval = STN_INVALID;
        break;
      }
      dig -= '0';
      if (result > lastval || (result == lastval && dig > remainder)) {
        errval = STN_TOOBIG;
        break;
      } else {
        result *= 10;
        result += dig;
      }
    }
    if (result < minval)
      errval = STN_TOOSMALL;
  }

done:
  switch (errval) {
  case STN_VALID:
    if (errstrp != NULL)
      *errstrp = NULL;
    break;
  case STN_INVALID:
    result = 0;
    errno = EINVAL;
    if (errstrp != NULL)
      *errstrp = ("invalid value");
    break;
  case STN_TOOSMALL:
    result = 0;
    errno = ERANGE;
    if (errstrp != NULL)
      *errstrp = ("value too small");
    break;
  case STN_TOOBIG:
    result = 0;
    errno = ERANGE;
    if (errstrp != NULL)
      *errstrp = ("value too large");
    break;
  }
  return result;
}
