#include "s3sender_impl.h"

#include "collectd.h"
#include "plugin.h"
#include "utils/common/common.h"

static int value_list_to_metric_name(char *buffer, size_t buffer_size,
                                     value_list_t const *vl) {
  int status;

  status = FORMAT_VL(buffer, buffer_size, vl);
  if (status != 0)
    return status;

  return status;
}

static int value_list_to_string(char *buffer, int buffer_len,
                                const data_set_t *ds, const value_list_t *vl) {
  int offset = 0;
  int status;

  assert(0 == strcmp(ds->type, vl->type));

  memset(buffer, '\0', buffer_len);


  status = FORMAT_VL(buffer, buffer_size, vl);
  buffer_sieze
  ptr_size -= strlen(ptr);
  ptr += strlen(ptr);

  for (size_t i = 0; i < ds->ds_num; i++) {
    if ((ds->ds[i].type != DS_TYPE_COUNTER) &&
        (ds->ds[i].type != DS_TYPE_GAUGE) &&
        (ds->ds[i].type != DS_TYPE_DERIVE) &&
        (ds->ds[i].type != DS_TYPE_ABSOLUTE)) {
      return -1;
    }

    if (i != 0) {
        status = snprintf(buffer + offset, buffer_len - offset, ",");
        if ((status < 1) || (status >= (buffer_len - offset))) {
            return -1;
        }
        offset += status;
    }

    if (ds->ds[i].type == DS_TYPE_GAUGE) {
      status = snprintf(buffer + offset, buffer_len - offset, ",%lf",
                        vl->values[i].gauge);
    } else if (ds->ds[i].type == DS_TYPE_COUNTER) {
      status = snprintf(buffer + offset, buffer_len - offset, ",%" PRIu64,
                        (uint64_t)vl->values[i].counter);
    } else if (ds->ds[i].type == DS_TYPE_DERIVE) {
      status = snprintf(buffer + offset, buffer_len - offset, ",%" PRIi64,
                        vl->values[i].derive);
    } else if (ds->ds[i].type == DS_TYPE_ABSOLUTE) {
      status = snprintf(buffer + offset, buffer_len - offset, ",%" PRIu64,
                        vl->values[i].absolute);
    }

    if ((status < 1) || (status >= (buffer_len - offset))) {
      return -1;
    }

    offset += status;
  }

  return 0;
}

static int s3sender_write(const data_set_t *ds, const value_list_t *vl, user_data_t *user_data) {
    char buffer[4096];
    if (value_list_to_string(buffer, sizeof(buffer), ds, vl) != 0)
        return -1;

    INFO("data:%s", buffer);

    return s3_write(buffer);
}

void module_register(void) {
    plugin_register_write("s3sender", s3sender_write, NULL);
}
