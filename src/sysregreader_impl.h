#ifndef SYSREGREADER_IMPL_H
#define SYSREGREADER_IMPL_H

#include "plugin.h"
#include "utils/common/common.h"

#ifdef __cplusplus 
extern "C" {
#endif

void* sr_create_implementation(void);
void sr_destroy_implementation(void *impl);

int sr_impl_configure(void *impl, const oconfig_item_t *ci);
int sr_impl_init(void *impl);
int sr_impl_read(void *impl);
int sr_impl_shutdown(void *impl);

#ifdef __cplusplus
}
#endif

#endif /* SYSREGREADER_IMPL_H */