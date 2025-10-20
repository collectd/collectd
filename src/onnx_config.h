#include "collectd.h"

#include "filter_chain.h"
#include "onnx_model.h"

struct plugin_config_s {
  char *output_family_name;
  char **input_names;
  size_t inputs_len;
  int64_t *input_shapes;
  char **output_names;
  size_t outputs_len;
  ort_model_config_t *model_config;
};

typedef struct plugin_config_s plugin_config_t;

int config_init(const oconfig_item_t *ci, plugin_config_t *cfg);