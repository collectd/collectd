#ifndef ONNX_MODEL_H
#define ONNX_MODEL_H 1

struct ort_model_config_s {
  char *model_path;
  int64_t *input_shapes;
  size_t inputs_len;
};
typedef struct ort_model_config_s ort_model_config_t;

struct ort_context_s;
typedef struct ort_context_s ort_context_t;

int onnx_init(ort_model_config_t *config, ort_context_t **ctx);
int onnx_destroy(ort_context_t *ortContext);
int onnx_run(ort_context_t *ortContext, float **inputs, float *outputs);

#endif