#include "collectd.h"

#include "filter_chain.h"
#include "onnx_config.h"
#include "onnx_model.h"
#include "utils/common/common.h"
#include "utils_cache.h"

struct onnx_user_data_s {
  plugin_config_t *config;
  ort_context_t *ort_context;
  float **inputs;
  float *outputs;
};

typedef struct onnx_user_data_s onnx_user_data_t;

void init_buffers(onnx_user_data_t *data, plugin_config_t *cfg) {
  float **inputs = calloc(cfg->inputs_len, sizeof(inputs[0]));
  for (size_t i = 0; i < cfg->inputs_len; i++) {
    inputs[i] = calloc(cfg->input_shapes[i], sizeof(inputs[0][0]));
  }
  data->inputs = inputs;
  data->outputs = calloc(cfg->outputs_len, sizeof(data->outputs[0]));
}

void destroy_buffers(onnx_user_data_t *data) {
  for (size_t i = 0; i < data->config->inputs_len; i++) {
    free(data->inputs[i]);
  }
  free(data->inputs);
  free(data->outputs);
}

static int tt_create(const oconfig_item_t *ci, void **user_data) /* {{{ */
{
  plugin_config_t *config = calloc(1, sizeof(*config));
  ort_model_config_t *model_config = calloc(1, sizeof(*model_config));
  config->model_config = model_config;

  int err = config_init(ci, config);
  if (err != 0) {
    ERROR("error parsing config");
    return 1;
  }

  ort_context_t *ort_context = NULL;
  err = onnx_init(model_config, &ort_context);
  if (err != 0) {
    ERROR("error initializing onnx");
    return 1;
  }

  onnx_user_data_t *data = malloc(sizeof(*data));
  data->config = config, data->ort_context = ort_context,
  init_buffers(data, config);

  *user_data = (void *)data;
  return 0;
} /* }}} int tt_create */

bool prepare_inputs(const metric_family_t *fam, plugin_config_t *cfg,
                    float **inputs) {
  bool any_matched = false;
  for (size_t i = 0; i < fam->metric.num && !any_matched; i++) {
    metric_t metric = fam->metric.ptr[i];
    strbuf_t buf = STRBUF_CREATE;
    metric_identity(&buf, &metric);
    for (size_t j = 0; j < cfg->inputs_len; j++) {
      if (strcmp(cfg->input_names[j], buf.ptr) == 0) {
        any_matched = true;
        break;
      }
    }
    STRBUF_DESTROY(buf);
  }
  if (!any_matched) {
    return false;
  }
  for (size_t i = 0; i < cfg->inputs_len; i++) {
    double tmp[cfg->input_shapes[i]];
    int err =
        uc_get_history_by_name(cfg->input_names[i], tmp, cfg->input_shapes[i]);
    if (err == ENOENT) {
      ERROR("metric with name %s not found", cfg->input_names[i]);
      return false;
    } else if (err != 0) {
      ERROR("error loading metric");
      return false;
    }
    for (size_t j = 0; j < cfg->input_shapes[i]; j++) {
      inputs[i][j] = (float)tmp[j];
    }
  }
  return true;
}

void create_outputs(char *fam_name, size_t outputs_len, char **output_names,
                    float *outputs) {
  metric_family_t famm = {
      .name = fam_name,
      .help = "outputs from onnx plugin",
      .unit = "1",
      .type = METRIC_TYPE_GAUGE,
  };

  for (size_t i = 0; i < outputs_len; i++) {
    metric_t metric = {.value.gauge = outputs[i]};
    label_set_add(&metric.label, "output_name", output_names[i]);
    metric_family_metric_append(&famm, metric);
  }

  int status = plugin_dispatch_metric_family(&famm);
  if (status != 0) {
    ERROR("onnx plugin: plugin_dispatch_metric_family failed: %s",
          STRERROR(status));
  }
  metric_family_metric_reset(&famm);
}

static int tt_invoke(metric_family_t const *fam, notification_meta_t **,
                     void **user_data) {
  onnx_user_data_t *data = *user_data;
  ort_context_t *ort_context = data->ort_context;
  plugin_config_t *config = data->config;

  if (!prepare_inputs(fam, config, data->inputs)) {
    return FC_TARGET_CONTINUE;
  }
  if (onnx_run(ort_context, data->inputs, data->outputs) != 0) {
    ERROR("error running onnx model");
    return -1;
  }
  create_outputs(config->output_family_name, config->outputs_len,
                 config->output_names, data->outputs);

  return FC_TARGET_CONTINUE;
} /* }}} int tt_invoke */

void config_destroy(plugin_config_t *config) {
  for (size_t i = 0; i < config->inputs_len; i++) {
    free(config->input_names[i]);
  }
  free(config->input_shapes);
  free(config->input_names);
  for (size_t i = 0; i < config->outputs_len; i++) {
    free(config->output_names[i]);
  }
  free(config->output_names);
  free(config->model_config);
  free(config);
}

static int tt_destroy(void **user_data) /* {{{ */
{
  onnx_user_data_t *data = *user_data;
  if (data == NULL) {
    return 0;
  }
  int err = onnx_destroy(data->ort_context);
  if (err != 0) {
    ERROR("error destroying onnx");
  }
  destroy_buffers(data);
  config_destroy(data->config);
  free(data);
  return 0;
} /* }}} int tt_destroy */

void module_register(void) {
  target_proc_t tproc = {0};

  tproc.create = tt_create;
  tproc.destroy = tt_destroy;
  tproc.invoke = tt_invoke;

  fc_register_target("target_onnx", tproc);
} /* module_register */
