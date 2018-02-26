/**
 * Copyright 2017 Florian Forster
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * Authors:
 *   Florian octo Forster <octo at collectd.org>
 **/

#include "config.h"

#if !defined(__GNUC__) || !__GNUC__
#define __attribute__(x) /**/
#endif

#include "collectd/lcc_features.h"
#include "collectd/network_parse.h"
#include "globals.h"

#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* for be{16,64}toh */
#if HAVE_ENDIAN_H
#include <endian.h>
#elif HAVE_SYS_ENDIAN_H
#include <sys/endian.h>
#else /* fallback */
__attribute__((const)) static uint16_t be16toh(uint16_t n) {
  uint8_t tmp[2];
  memmove(tmp, &n, sizeof(tmp));

  return ((uint16_t)tmp[0] << 8) | ((uint16_t)tmp[1] << 0);
}

__attribute__((const)) static uint64_t be64toh(uint64_t n) {
  uint8_t tmp[8];
  memmove(tmp, &n, sizeof(tmp));

  return ((uint64_t)tmp[0] << 56) | ((uint64_t)tmp[1] << 48) |
         ((uint64_t)tmp[2] << 40) | ((uint64_t)tmp[3] << 32) |
         ((uint64_t)tmp[4] << 24) | ((uint64_t)tmp[5] << 16) |
         ((uint64_t)tmp[6] << 8) | ((uint64_t)tmp[7] << 0);
}
#endif

#if HAVE_GCRYPT_H
#define GCRYPT_NO_DEPRECATED
#include <gcrypt.h>
#endif

#include <stdio.h>
#define DEBUG(...) printf(__VA_ARGS__)

#if HAVE_GCRYPT_H
#if GCRYPT_VERSION_NUMBER < 0x010600
GCRY_THREAD_OPTION_PTHREAD_IMPL;
#endif
#endif

/* forward declaration because parse_sign_sha256()/parse_encrypt_aes256() and
 * network_parse() need to call each other. */
static int network_parse(void *data, size_t data_size, lcc_security_level_t sl,
                         lcc_network_parse_options_t const *opts);

#if HAVE_GCRYPT_H
static int init_gcrypt() {
  /* http://lists.gnupg.org/pipermail/gcrypt-devel/2003-August/000458.html
   * Because you can't know in a library whether another library has
   * already initialized the library */
  if (gcry_control(GCRYCTL_ANY_INITIALIZATION_P))
    return (0);

/* http://www.gnupg.org/documentation/manuals/gcrypt/Multi_002dThreading.html
 * To ensure thread-safety, it's important to set GCRYCTL_SET_THREAD_CBS
 * *before* initalizing Libgcrypt with gcry_check_version(), which itself must
 * be called before any other gcry_* function. GCRYCTL_ANY_INITIALIZATION_P
 * above doesn't count, as it doesn't implicitly initalize Libgcrypt.
 *
 * tl;dr: keep all these gry_* statements in this exact order please. */
#if GCRYPT_VERSION_NUMBER < 0x010600
  if (gcry_control(GCRYCTL_SET_THREAD_CBS, &gcry_threads_pthread)) {
    return -1;
  }
#endif

  gcry_check_version(NULL);

  if (gcry_control(GCRYCTL_INIT_SECMEM, 32768)) {
    return -1;
  }

  gcry_control(GCRYCTL_INITIALIZATION_FINISHED);
  return 0;
}
#endif

typedef struct {
  uint8_t *data;
  size_t len;
} buffer_t;

static int buffer_next(buffer_t *b, void *out, size_t n) {
  if (b->len < n) {
    return -1;
  }
  memmove(out, b->data, n);

  b->data += n;
  b->len -= n;

  return 0;
}

static int buffer_uint16(buffer_t *b, uint16_t *out) {
  uint16_t tmp;
  if (buffer_next(b, &tmp, sizeof(tmp)) != 0)
    return -1;

  *out = be16toh(tmp);
  return 0;
}

#define TYPE_HOST 0x0000
#define TYPE_TIME 0x0001
#define TYPE_TIME_HR 0x0008
#define TYPE_PLUGIN 0x0002
#define TYPE_PLUGIN_INSTANCE 0x0003
#define TYPE_TYPE 0x0004
#define TYPE_TYPE_INSTANCE 0x0005
#define TYPE_VALUES 0x0006
#define TYPE_INTERVAL 0x0007
#define TYPE_INTERVAL_HR 0x0009
#define TYPE_SIGN_SHA256 0x0200
#define TYPE_ENCR_AES256 0x0210

static int parse_int(void *payload, size_t payload_size, uint64_t *out) {
  uint64_t tmp;

  if (payload_size != sizeof(tmp))
    return EINVAL;

  memmove(&tmp, payload, sizeof(tmp));
  *out = be64toh(tmp);
  return 0;
}

static int parse_string(void *payload, size_t payload_size, char *out,
                        size_t out_size) {
  char *in = payload;

  if ((payload_size < 1) || (in[payload_size - 1] != 0) ||
      (payload_size > out_size))
    return EINVAL;

  strncpy(out, in, out_size);
  return 0;
}

static int parse_identifier(uint16_t type, void *payload, size_t payload_size,
                            lcc_value_list_t *state) {
  char buf[LCC_NAME_LEN];

  if (parse_string(payload, payload_size, buf, sizeof(buf)) != 0)
    return EINVAL;

  switch (type) {
  case TYPE_HOST:
    memmove(state->identifier.host, buf, LCC_NAME_LEN);
    break;
  case TYPE_PLUGIN:
    memmove(state->identifier.plugin, buf, LCC_NAME_LEN);
    break;
  case TYPE_PLUGIN_INSTANCE:
    memmove(state->identifier.plugin_instance, buf, LCC_NAME_LEN);
    break;
  case TYPE_TYPE:
    memmove(state->identifier.type, buf, LCC_NAME_LEN);
    break;
  case TYPE_TYPE_INSTANCE:
    memmove(state->identifier.type_instance, buf, LCC_NAME_LEN);
    break;
  default:
    return EINVAL;
  }

  return 0;
}

static int parse_time(uint16_t type, void *payload, size_t payload_size,
                      lcc_value_list_t *state) {
  uint64_t tmp = 0;
  if (parse_int(payload, payload_size, &tmp))
    return EINVAL;

  double t = (double)tmp;
  switch (type) {
  case TYPE_INTERVAL:
    state->interval = t;
    break;
  case TYPE_INTERVAL_HR:
    state->interval = t / 1073741824.0;
    break;
  case TYPE_TIME:
    state->time = t;
    break;
  case TYPE_TIME_HR:
    state->time = t / 1073741824.0;
    break;
  default:
    return EINVAL;
  }

  return 0;
}

static double ntohd(double val) /* {{{ */
{
  static int config = 0;

  union {
    uint8_t byte[8];
    double floating;
  } in = {
      .floating = val,
  };
  union {
    uint8_t byte[8];
    double floating;
  } out = {
      .byte = {0},
  };

  if (config == 0) {
    double d = 8.642135e130;
    uint8_t b[8];

    memcpy(b, &d, sizeof(b));

    if ((b[0] == 0x2f) && (b[1] == 0x25) && (b[2] == 0xc0) && (b[3] == 0xc7) &&
        (b[4] == 0x43) && (b[5] == 0x2b) && (b[6] == 0x1f) && (b[7] == 0x5b))
      config = 1; /* need nothing */
    else if ((b[7] == 0x2f) && (b[6] == 0x25) && (b[5] == 0xc0) &&
             (b[4] == 0xc7) && (b[3] == 0x43) && (b[2] == 0x2b) &&
             (b[1] == 0x1f) && (b[0] == 0x5b))
      config = 2; /* endian flip */
    else if ((b[4] == 0x2f) && (b[5] == 0x25) && (b[6] == 0xc0) &&
             (b[7] == 0xc7) && (b[0] == 0x43) && (b[1] == 0x2b) &&
             (b[2] == 0x1f) && (b[3] == 0x5b))
      config = 3; /* int swap */
    else
      config = 4;
  }

  if (memcmp((char[]){0, 0, 0, 0, 0, 0, 0xf8, 0x7f}, in.byte, 8) == 0) {
    return NAN;
  } else if (config == 1) {
    return val;
  } else if (config == 2) {
    in.floating = val;
    out.byte[0] = in.byte[7];
    out.byte[1] = in.byte[6];
    out.byte[2] = in.byte[5];
    out.byte[3] = in.byte[4];
    out.byte[4] = in.byte[3];
    out.byte[5] = in.byte[2];
    out.byte[6] = in.byte[1];
    out.byte[7] = in.byte[0];
    return (out.floating);
  } else if (config == 3) {
    in.floating = val;
    out.byte[0] = in.byte[4];
    out.byte[1] = in.byte[5];
    out.byte[2] = in.byte[6];
    out.byte[3] = in.byte[7];
    out.byte[4] = in.byte[0];
    out.byte[5] = in.byte[1];
    out.byte[6] = in.byte[2];
    out.byte[7] = in.byte[3];
    return out.floating;
  } else {
    /* If in doubt, just copy the value back to the caller. */
    return val;
  }
} /* }}} double ntohd */

static int parse_values(void *payload, size_t payload_size,
                        lcc_value_list_t *state) {
  buffer_t *b = &(buffer_t){
      .data = payload, .len = payload_size,
  };

  uint16_t n;
  if (buffer_uint16(b, &n))
    return EINVAL;

  if (((size_t)n * 9) != b->len)
    return EINVAL;

  state->values_len = (size_t)n;
  state->values = calloc(sizeof(*state->values), state->values_len);
  state->values_types = calloc(sizeof(*state->values_types), state->values_len);
  if ((state->values == NULL) || (state->values_types == NULL)) {
    return ENOMEM;
  }

  for (uint16_t i = 0; i < n; i++) {
    uint8_t tmp;
    if (buffer_next(b, &tmp, sizeof(tmp)))
      return EINVAL;
    state->values_types[i] = (int)tmp;
  }

  for (uint16_t i = 0; i < n; i++) {
    uint64_t tmp;
    if (buffer_next(b, &tmp, sizeof(tmp)))
      return EINVAL;

    if (state->values_types[i] == LCC_TYPE_GAUGE) {
      union {
        uint64_t i;
        double d;
      } conv = {.i = tmp};
      state->values[i].gauge = ntohd(conv.d);
      continue;
    }

    tmp = be64toh(tmp);
    switch (state->values_types[i]) {
    case LCC_TYPE_COUNTER:
      state->values[i].counter = (counter_t)tmp;
      break;
    case LCC_TYPE_DERIVE:
      state->values[i].derive = (derive_t)tmp;
      break;
    case LCC_TYPE_ABSOLUTE:
      state->values[i].absolute = (absolute_t)tmp;
      break;
    default:
      return EINVAL;
    }
  }

  return 0;
}

#if HAVE_GCRYPT_H
static int verify_sha256(void *payload, size_t payload_size,
                         char const *username, char const *password,
                         uint8_t hash_provided[32]) {
  gcry_md_hd_t hd = NULL;

  gcry_error_t err = gcry_md_open(&hd, GCRY_MD_SHA256, GCRY_MD_FLAG_HMAC);
  if (err != 0) {
    return (int)err;
  }

  err = gcry_md_setkey(hd, password, strlen(password));
  if (err != 0) {
    gcry_md_close(hd);
    return (int)err;
  }

  gcry_md_write(hd, username, strlen(username));
  gcry_md_write(hd, payload, payload_size);

  unsigned char *hash_calculated = gcry_md_read(hd, GCRY_MD_SHA256);
  if (!hash_calculated) {
    gcry_md_close(hd);
    return -1;
  }

  int ret = memcmp(hash_provided, hash_calculated, 32);

  gcry_md_close(hd);
  hash_calculated = NULL;

  return !!ret;
}
#else /* !HAVE_GCRYPT_H */
static int verify_sha256(void *payload, size_t payload_size,
                         char const *username, char const *password,
                         uint8_t hash_provided[32]) {
  return ENOTSUP;
}
#endif

static int parse_sign_sha256(void *signature, size_t signature_len,
                             void *payload, size_t payload_size,
                             lcc_network_parse_options_t const *opts) {
  if (opts->password_lookup == NULL) {
    /* The sender signed the packet but we can't verify it. Handle it as if it
     * were unsigned, i.e. security level NONE. */
    return network_parse(payload, payload_size, NONE, opts);
  }

  buffer_t *b = &(buffer_t){
      .data = signature, .len = signature_len,
  };

  uint8_t hash[32];
  if (buffer_next(b, hash, sizeof(hash)))
    return EINVAL;

  char username[b->len + 1];
  memset(username, 0, sizeof(username));
  if (buffer_next(b, username, sizeof(username) - 1)) {
    return EINVAL;
  }

  char const *password = opts->password_lookup(username);
  if (!password)
    return network_parse(payload, payload_size, NONE, opts);

  int status = verify_sha256(payload, payload_size, username, password, hash);
  if (status != 0)
    return status;

  return network_parse(payload, payload_size, SIGN, opts);
}

#if HAVE_GCRYPT_H
static int decrypt_aes256(buffer_t *b, void *iv, size_t iv_size,
                          char const *password) {
  gcry_cipher_hd_t cipher = NULL;

  if (gcry_cipher_open(&cipher, GCRY_CIPHER_AES256, GCRY_CIPHER_MODE_OFB,
                       /* flags = */ 0))
    return -1;

  uint8_t pwhash[32] = {0};
  gcry_md_hash_buffer(GCRY_MD_SHA256, pwhash, password, strlen(password));

  fprintf(stderr, "sizeof(iv) = %" PRIsz "\n", sizeof(iv));
  if (gcry_cipher_setkey(cipher, pwhash, sizeof(pwhash)) ||
      gcry_cipher_setiv(cipher, iv, iv_size) ||
      gcry_cipher_decrypt(cipher, b->data, b->len, /* in = */ NULL,
                          /* in_size = */ 0)) {
    gcry_cipher_close(cipher);
    return -1;
  }

  gcry_cipher_close(cipher);
  return 0;
}

static int parse_encrypt_aes256(void *data, size_t data_size,
                                lcc_network_parse_options_t const *opts) {
  if (opts->password_lookup == NULL) {
    /* Without a password source it's (hopefully) impossible to decrypt the
     * network packet. */
    return ENOENT;
  }

  buffer_t *b = &(buffer_t){
      .data = data, .len = data_size,
  };

  uint16_t username_len;
  if (buffer_uint16(b, &username_len))
    return EINVAL;
  if ((size_t)username_len > data_size)
    return ENOMEM;
  char username[((size_t)username_len) + 1];
  memset(username, 0, sizeof(username));
  if (buffer_next(b, username, (size_t)username_len))
    return EINVAL;

  char const *password = opts->password_lookup(username);
  if (!password)
    return ENOENT;

  uint8_t iv[16];
  if (buffer_next(b, iv, sizeof(iv)))
    return EINVAL;

  int status = decrypt_aes256(b, iv, sizeof(iv), password);
  if (status != 0)
    return status;

  uint8_t hash_provided[20];
  if (buffer_next(b, hash_provided, sizeof(hash_provided))) {
    return -1;
  }

  uint8_t hash_calculated[20];
  gcry_md_hash_buffer(GCRY_MD_SHA1, hash_calculated, b->data, b->len);

  if (memcmp(hash_provided, hash_calculated, sizeof(hash_provided)) != 0) {
    return -1;
  }

  return network_parse(b->data, b->len, ENCRYPT, opts);
}
#else /* !HAVE_GCRYPT_H */
static int parse_encrypt_aes256(void *data, size_t data_size,
                                lcc_network_parse_options_t const *opts) {
  return ENOTSUP;
}
#endif

static int network_parse(void *data, size_t data_size, lcc_security_level_t sl,
                         lcc_network_parse_options_t const *opts) {
  buffer_t *b = &(buffer_t){
      .data = data, .len = data_size,
  };

  lcc_value_list_t state = {0};

  while (b->len > 0) {
    uint16_t type = 0, sz = 0;
    if (buffer_uint16(b, &type) || buffer_uint16(b, &sz)) {
      DEBUG("lcc_network_parse(): reading type and/or length failed.\n");
      return EINVAL;
    }

    if ((sz < 5) || (((size_t)sz - 4) > b->len)) {
      DEBUG("lcc_network_parse(): invalid 'sz' field: sz = %" PRIu16
            ", b->len = %" PRIsz "\n",
            sz, b->len);
      return EINVAL;
    }
    sz -= 4;

    uint8_t payload[sz];
    if (buffer_next(b, payload, sizeof(payload)))
      return EINVAL;

    switch (type) {
    case TYPE_HOST:
    case TYPE_PLUGIN:
    case TYPE_PLUGIN_INSTANCE:
    case TYPE_TYPE:
    case TYPE_TYPE_INSTANCE: {
      if (parse_identifier(type, payload, sizeof(payload), &state)) {
        DEBUG("lcc_network_parse(): parse_identifier failed.\n");
        return EINVAL;
      }
      break;
    }

    case TYPE_INTERVAL:
    case TYPE_INTERVAL_HR:
    case TYPE_TIME:
    case TYPE_TIME_HR: {
      if (parse_time(type, payload, sizeof(payload), &state)) {
        DEBUG("lcc_network_parse(): parse_time failed.\n");
        return EINVAL;
      }
      break;
    }

    case TYPE_VALUES: {
      lcc_value_list_t vl = state;
      if (parse_values(payload, sizeof(payload), &vl)) {
        free(vl.values);
        free(vl.values_types);
        DEBUG("lcc_network_parse(): parse_values failed.\n");
        return EINVAL;
      }

      int status = 0;

      /* Write metrics if they have the required security level. */
      if (sl >= opts->security_level)
        status = opts->writer(&vl);

      free(vl.values);
      free(vl.values_types);

      if (status != 0)
        return status;
      break;
    }

    case TYPE_SIGN_SHA256: {
      int status =
          parse_sign_sha256(payload, sizeof(payload), b->data, b->len, opts);
      if (status != 0) {
        DEBUG("lcc_network_parse(): parse_sign_sha256() = %d\n", status);
        return -1;
      }
      /* parse_sign_sha256, if successful, consumes all remaining data. */
      b->data = NULL;
      b->len = 0;
      break;
    }

    case TYPE_ENCR_AES256: {
      int status = parse_encrypt_aes256(payload, sizeof(payload), opts);
      if (status != 0) {
        DEBUG("lcc_network_parse(): parse_encrypt_aes256() = %d\n", status);
        return -1;
      }
      break;
    }

    default: {
      DEBUG("lcc_network_parse(): ignoring unknown type %" PRIu16 "\n", type);
      return EINVAL;
    }
    }
  }

  return 0;
}

int lcc_network_parse(void *data, size_t data_size,
                      lcc_network_parse_options_t opts) {
  if (opts.password_lookup) {
#if HAVE_GCRYPT_H
    int status;
    if ((status = init_gcrypt())) {
      return status;
    }
#else
    return ENOTSUP;
#endif
  }

  return network_parse(data, data_size, NONE, &opts);
}
