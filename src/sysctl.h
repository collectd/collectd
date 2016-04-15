#ifndef __SYSCTL_H___

#define __SYSCTL_H__ 1
int slashdot(char *, char , char );
static int sysctl_config (oconfig_item_t *ci);

struct sysctl_t {
  char *name;
  int index;
};

#endif
