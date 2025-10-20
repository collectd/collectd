#ifndef ONNX_CONFIG_H
#define ONNX_CONFIG_H 1

#include "onnx_config.h"
#include "string.h"

int ci_get_string(const oconfig_value_t *val, char **string) {
  if (val->type != OCONFIG_TYPE_STRING) {
    ERROR("value has to have type `string`");
    return 1;
  }
  *string = strdup(val->value.string);
  return 0;
}

int ci_get_int(const oconfig_value_t *val, int64_t *num) {
  if (val->type != OCONFIG_TYPE_NUMBER) {
    ERROR("value has to have type number");
    return 1;
  }
  *num = (int64_t)val->value.number;
  return 0;
}

int ci_get_input(const oconfig_item_t *input, char **input_name,
                 int64_t *input_shape) {
  if (input->children_num != 2) {
    ERROR("error parsing input config: input config has fields, exactly 2 "
          "fields required, Name and Size");
    return 1;
  }

  *input_name = NULL;
  *input_shape = -1;

  int err = 0;
  for (size_t j = 0; j < 2; j++) {
    oconfig_item_t child = input->children[j];
    if (strcmp(child.key, "Name") == 0) {
      err = ci_get_string(child.values, input_name);
    } else if (strcmp(child.key, "Size") == 0) {
      err = ci_get_int(child.values, input_shape);
    } else {
      ERROR("Unknown key in input config");
      err = 1;
    }

    if (err != 0) {
      break;
    }
  }

  if (*input_name == NULL) {
    ERROR("Input name was not set");
    err = 1;
  }

  if (*input_shape == -1) {
    ERROR("Input shape was not set");
    err = 1;
  }

  if (err != 0) {
    ERROR("Error parsing input config");
  }
  return err;
}

int ci_get_output(const oconfig_item_t *output, char **output_name) {
  if (output->children_num != 1) {
    ERROR("error parsing output config: output has %d fields, exactly 1 field "
          "required: `Name`",
          output->children_num);
    return 1;
  }

  *output_name = NULL;

  int err = 0;

  oconfig_item_t child = *output->children;
  if (strcmp(child.key, "Name") == 0) {
    err = ci_get_string(child.values, output_name);
  } else {
    ERROR("Unknown key `%s` in output config", child.key);
    err = 1;
  }

  if (err != 0) {
    ERROR("Error parsing input config");
  }
  return err;
}

int ci_get_inputs(const oconfig_item_t *ci, plugin_config_t *cfg) {
  size_t num = ci->children_num;
  if (num == 0) {
    ERROR("there has to be at least one input");
    return 1;
  }

  int64_t *input_shapes = calloc(num, sizeof(*input_shapes));
  char **input_names = calloc(num, sizeof(*input_names));
  for (size_t i = 0; i < num; i++) {
    const oconfig_item_t input = ci->children[i];
    if (ci_get_input(&input, &input_names[i], &input_shapes[i])) {
      ERROR("error parsing inputs config");
      return 1;
    }
  }

  cfg->inputs_len = num;
  cfg->input_shapes = input_shapes;
  cfg->input_names = input_names;

  return 0;
}

int ci_get_outputs(const oconfig_item_t *ci, plugin_config_t *cfg) {
  size_t num = ci->children_num;
  char **output_names = calloc(num, sizeof(*output_names));

  for (size_t i = 0; i < num; i++) {
    const oconfig_item_t output = ci->children[i];
    if (ci_get_output(&output, &output_names[i])) {
      ERROR("error parsing outputs config");
      return 1;
    }
  }

  cfg->outputs_len = num;
  cfg->output_names = output_names;
  return 0;
}

void print_config(const oconfig_item_t *ci, int depth) {
  char prefix[depth * 2 + 1];
  for (int i = 0; i < depth * 2; i++) {
    prefix[i] = ' ';
  }
  prefix[depth * 2] = '\0';
  printf("%s%s: ", prefix, ci->key);
  if (ci->values_num > 0) {
    for (int i = 0; i < ci->values_num; i++) {
      oconfig_value_t val = ci->values[i];
      switch (val.type) {
      case OCONFIG_TYPE_STRING:
        printf("%s ", val.value.string);
        break;
      case OCONFIG_TYPE_NUMBER:
        printf("%lf ", val.value.number);
        break;
      case OCONFIG_TYPE_BOOLEAN:
        printf("%i ", val.value.boolean);
        break;
      }
    }
  }
  printf("\n");
  if (ci->children_num > 0) {
    for (int i = 0; i < ci->children_num; i++) {
      print_config(&ci->children[i], depth + 1);
    }
  }
}

int config_init(const oconfig_item_t *ci, plugin_config_t *cfg) {
  print_config(ci, 0);

  int err = 0;
  for (size_t i = 0; i < ci->children_num; i++) {
    oconfig_item_t child = ci->children[i];

    if (strcmp(child.key, "ModelPath") == 0) {
      err = ci_get_string(&child.values[0], &cfg->model_config->model_path);
    } else if (strcmp(child.key, "OutputFamilyName") == 0) {
      err = ci_get_string(&child.values[0], &cfg->output_family_name);
    } else if (strcmp(child.key, "Inputs") == 0) {
      err = ci_get_inputs(&child, cfg);
    } else if (strcmp(child.key, "Outputs") == 0) {
      err = ci_get_outputs(&child, cfg);
    } else {
      ERROR("Unknown key `%s` in config", child.key);
      err = 1;
    }

    if (err != 0) {
      ERROR("Error parsing config");
      return err;
    }
  }

  cfg->model_config->inputs_len = cfg->inputs_len;
  cfg->model_config->input_shapes = cfg->input_shapes;

  return 0;
}

#endif