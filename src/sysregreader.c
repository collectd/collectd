#include "plugin.h"
#include "utils/common/common.h"
#include "sysregreader_impl.h"

#define PLUGIN_NAME "sysregreader"

static void *g_impl = NULL;

static int sr_config_callback(oconfig_item_t *ci) {
    if (g_impl == NULL) {
        g_impl = sr_create_implementation();
        if (g_impl == NULL) {
            ERROR("sysregreader plugin: Failed to create implementation");
            return -1;
        }
    }
    
    int ret = sr_impl_configure(g_impl, ci);
    if (ret != 0) {
        ERROR("sysregreader plugin: Configuration failed");
        return -1;
    }
    return 0;
}

static int sr_init(void) {
    if (g_impl == NULL) {
        g_impl = sr_create_implementation();
        if (g_impl == NULL) {
            ERROR("sysregreader plugin: Failed to create implementation");
            return -1;
        }
    }
    
    if (sr_impl_init(g_impl) != 0) {
        ERROR("sysregreader plugin: Initialization failed");
        return -1;
    }
    return 0;
}

static int sr_read(void) {
    if (g_impl == NULL) {
        ERROR("sysregreader plugin: Implementation not initialized");
        return -1;
    }
    
    if (sr_impl_read(g_impl) != 0) {
        ERROR("sysregreader plugin: Read failed");
        return -1;
    }
    return 0;
}

static int sr_shutdown(void) {
    int status = 0;
    if (g_impl != NULL) {
        status = sr_impl_shutdown(g_impl);
        sr_destroy_implementation(g_impl);
        g_impl = NULL;
    }
    return status;
}

void module_register(void) {
    plugin_register_init(PLUGIN_NAME, sr_init);
    plugin_register_complex_config(PLUGIN_NAME, sr_config_callback);
    plugin_register_read(PLUGIN_NAME, sr_read);
    plugin_register_shutdown(PLUGIN_NAME, sr_shutdown);
}