#define _GNU_SOURCE

#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <error.h>
#include <stdio.h>
#include <string.h>

#define PREFIX "/rootfs"
#define BUFSIZE 256

const char *add_prefix(const char *orig, char *prefixed) {
  int status;
  int errno;

  if ((strncmp(orig, "/proc", 5) == 0) || (strncmp(orig, "/sys", 4) == 0)) {

    status = snprintf(prefixed, BUFSIZE, "%s%s", PREFIX, orig);
    if ((unsigned int)status >= BUFSIZE) {
      error(status, ENAMETOOLONG,
            "'%s' got truncated when adding '%s' prefix: '%s'", orig, PREFIX,
            prefixed);
      return orig;
    } else if (status < 1) {
      error(status, errno,
            "adding '%s' prefix to file path failed: '%s' -> '%s'", PREFIX,
            orig, prefixed);
      return orig;
    } else {
      return (const char *)prefixed;
    }

  } else {
    return orig;
  }
}

FILE *fopen(const char *path, const char *mode) {
  char filename[BUFSIZE] = "\0";

  FILE *(*original_fopen)(const char *, const char *);
  original_fopen = dlsym(RTLD_NEXT, "fopen");

  return (*original_fopen)(add_prefix(path, filename), mode);
}

DIR *opendir(const char *name) {
  char filename[BUFSIZE] = "\0";

  DIR *(*original_opendir)(const char *);
  original_opendir = dlsym(RTLD_NEXT, "opendir");

  return (*original_opendir)(add_prefix(name, filename));
}

int *open(const char *pathname, int flags) {
  char filename[BUFSIZE] = "\0";

  int *(*original_open)(const char *, int);
  original_open = dlsym(RTLD_NEXT, "open");

  return (*original_open)(add_prefix(pathname, filename), flags);
}
