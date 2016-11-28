
#ifndef VIRT2_TEST_H
#define VIRT2_TEST_H

#define UUID_STRLEN 36
typedef struct fakeVirDomain *fakeVirDomainPtr;
struct fakeVirDomain {
  char uuid[UUID_STRLEN + 1];
  char *name;
  char *xml;
};

#endif // VIRT2_TEST_H

