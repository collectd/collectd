
#include "virt_test.h"

typedef struct VMInfo VMInfo;
typedef struct virDomainStatsRecord virDomainStatsRecord;
typedef virDomainStatsRecord *virDomainStatsRecordPtr;

int vminfo_parse(VMInfo *vm, const virDomainStatsRecordPtr record,
                 int extrainfo) {
  return 0;
}

void vminfo_init(VMInfo *vm) { return; }

void vminfo_free(VMInfo *vm) { return; }
