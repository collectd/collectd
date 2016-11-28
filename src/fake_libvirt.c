
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "virt_test.h"

typedef struct fakeVirDomain *virDomainPtr;

int virDomainGetUUIDString(virDomainPtr _dom, char *out) {
  if (!_dom || !out)
    return -1;
  fakeVirDomainPtr dom = (fakeVirDomainPtr)_dom;
  strncpy(out, dom->uuid, UUID_STRLEN);
  return 0;
}

const char *virDomainGetXMLDesc(virDomainPtr _dom) {
  if (!_dom)
    return NULL;
  fakeVirDomainPtr dom = (fakeVirDomainPtr)_dom;
  return strdup(dom->xml);
}

const char *virDomainGetName(virDomainPtr _dom) {
  if (!_dom)
    return NULL;
  fakeVirDomainPtr dom = (fakeVirDomainPtr)_dom;
  return dom->name;
}
