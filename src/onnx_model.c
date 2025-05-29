#include "onnxruntime/onnxruntime_c_api.h"
#include "utils/common/common.h"

#include "onnx_model.h"

#define ONNX_CHECK_ERROR(err)                                                  \
  {                                                                            \
    if (api == NULL) {                                                         \
      ERROR("ONNX_CHECK_ERROR was called with api == NULL");                   \
      return 1;                                                                \
    }                                                                          \
    if (err != NULL) {                                                         \
      ERROR("ONNX error occured: %s", api->GetErrorMessage(err));              \
      api->ReleaseStatus(err);                                                 \
      return 1;                                                                \
    }                                                                          \
  }

struct ort_model_s {
  OrtSession *session;

  size_t inputs_len;
  char **input_names;
  int64_t *input_shapes;
  OrtValue **input_tensors;

  size_t outputs_len;
  char **output_names;
  OrtValue **output_tensors;
};
typedef struct ort_model_s ort_model_t;

struct ort_context_s {
  const OrtApi *api;
  OrtEnv *env;
  OrtAllocator *allocator;
  ort_model_t *model;
};
typedef struct ort_context_s ort_context_t;

OrtStatusPtr model_prepare_tensors(const OrtApi *api, ort_model_t *model,
                                   OrtAllocator *allocator) {
  OrtStatusPtr err = NULL;

  model->input_tensors =
      calloc(model->inputs_len, sizeof(*model->input_tensors));
  for (size_t i = 0; i < model->inputs_len; i++) {
    const int64_t shape[2] = {1, model->input_shapes[i]};
    err = api->CreateTensorAsOrtValue(allocator, shape, 2,
                                      ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                      &model->input_tensors[i]);
    if (err != NULL) {
      return err;
    }
  }

  model->output_tensors =
      calloc(model->outputs_len, sizeof(*model->output_tensors));
  for (size_t i = 0; i < model->outputs_len; i++) {
    const int64_t shape[2] = {1, 1}; // outputs are always single-valued
    err = api->CreateTensorAsOrtValue(allocator, shape, 2,
                                      ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                      &model->output_tensors[i]);
    if (err != NULL) {
      return err;
    }
  }

  return err;
}

OrtStatusPtr model_prepare_names(const OrtApi *api, ort_model_t *model,
                                 OrtAllocator *allocator) {
  OrtStatusPtr err = NULL;
  err = api->SessionGetInputCount(model->session, &model->inputs_len);
  if (err != NULL) {
    return err;
  }

  model->input_names = malloc(sizeof(*model->input_names) * model->inputs_len);
  for (size_t i = 0; i < model->inputs_len; i++) {
    err = api->SessionGetInputName(model->session, i, allocator,
                                   &model->input_names[i]);
    if (err != NULL) {
      return err;
    }
  }

  err = api->SessionGetOutputCount(model->session, &model->outputs_len);
  if (err != NULL) {
    return err;
  }

  model->output_names =
      malloc(sizeof(*model->output_names) * model->outputs_len);
  for (size_t i = 0; i < model->outputs_len; i++) {
    err = api->SessionGetOutputName(model->session, i, allocator,
                                    &model->output_names[i]);
    if (err != NULL) {
      return err;
    }
  }

  return err;
}

OrtStatusPtr model_create(const OrtApi *api, ort_context_t *context,
                          ort_model_config_t *cfg) {
  OrtStatusPtr err = NULL;
  ort_model_t *model = calloc(1, sizeof(*model));

  OrtSessionOptions *sessionOpts;
  err = api->CreateSessionOptions(&sessionOpts);
  if (err != NULL)
    return err;

  err = api->CreateSession(context->env, cfg->model_path, sessionOpts,
                           &model->session);
  if (err != NULL)
    return err;

  api->ReleaseSessionOptions(sessionOpts);
  if (err != NULL)
    return err;

  err = model_prepare_names(api, model, context->allocator);
  if (err != NULL)
    return err;

  if (model->inputs_len != cfg->inputs_len) {
    ERROR("model and config inputs do no match");
    abort();
  }
  model->input_shapes = calloc(cfg->inputs_len, sizeof(*model->input_shapes));
  for (size_t i = 0; i < cfg->inputs_len; i++) {
    model->input_shapes[i] = cfg->input_shapes[i];
  }

  err = model_prepare_tensors(api, model, context->allocator);
  if (err != NULL)
    return err;

  printf("Created model: %s\n", cfg->model_path);
  printf("Inputs:\n");
  for (size_t i = 0; i < model->inputs_len; i++) {
    printf("\t%ld:\t%s\n", i, model->input_names[i]);
  }
  printf("Outputs:\n");
  for (size_t i = 0; i < model->outputs_len; i++) {
    printf("\t%ld:\t%s\n", i, model->output_names[i]);
  }

  context->model = model;
  return err;
}

int onnx_init(ort_model_config_t *cfg, ort_context_t **out_context) {
  const OrtApi *api = OrtGetApiBase()->GetApi(ORT_API_VERSION);

  ort_context_t *context = calloc(1, sizeof(*context));
  *out_context = context;

  context->api = api;

  OrtStatusPtr err =
      api->CreateEnv(ORT_LOGGING_LEVEL_INFO, "target_onnx", &context->env);
  ONNX_CHECK_ERROR(err);

  err = api->GetAllocatorWithDefaultOptions(&context->allocator);
  ONNX_CHECK_ERROR(err);

  err = model_create(api, context, cfg);
  ONNX_CHECK_ERROR(err);

  return 0;
}

int onnx_destroy(ort_context_t *ort_context) {
  const OrtApi *api = ort_context->api;
  ort_model_t *model = ort_context->model;

  for (size_t i = 0; i < model->inputs_len; i++) {
    api->ReleaseValue(model->input_tensors[i]);
  }
  free(model->input_tensors);
  for (size_t i = 0; i < model->inputs_len; i++) {
    api->ReleaseValue(model->output_tensors[i]);
  }
  free(model->output_tensors);
  for (size_t i = 0; i < model->inputs_len; i++) {
    OrtStatusPtr err =
        api->AllocatorFree(ort_context->allocator, model->input_names[i]);
    ONNX_CHECK_ERROR(err);
  }
  for (size_t i = 0; i < model->outputs_len; i++) {
    OrtStatusPtr err =
        api->AllocatorFree(ort_context->allocator, model->output_names[i]);
    ONNX_CHECK_ERROR(err);
  }
  free(model->input_names);
  free(model->output_names);

  api->ReleaseEnv(ort_context->env);
  api->ReleaseSession(model->session);
  api->ReleaseAllocator(ort_context->allocator);

  free(model);
  free(ort_context);
  return 0;
}

int onnx_run(ort_context_t *context, float **inputs, float *outputs) {
  const OrtApi *api = context->api;
  ort_model_t *model = context->model;

  for (size_t i = 0; i < model->inputs_len; i++) {
    float *input_buffer = NULL;

    OrtStatusPtr err = api->GetTensorMutableData(model->input_tensors[i],
                                                 (void **)&input_buffer);
    ONNX_CHECK_ERROR(err);

    bool all_nan = true;
    for (size_t j = 0; j < model->input_shapes[i]; j++) {
      input_buffer[j] = inputs[i][j];
      if (!isnan(input_buffer[j])) {
        all_nan = false;
      }
    }
    if (all_nan) {
      WARNING("warning: all input values for %s are NaN",
              model->input_names[i]);
    }
  }

  OrtStatusPtr err =
      api->Run(model->session, NULL, (const char *const *)model->input_names,
               (const OrtValue *const *)model->input_tensors, model->inputs_len,
               (const char *const *)model->output_names, model->outputs_len,
               model->output_tensors);
  ONNX_CHECK_ERROR(err);

  for (size_t i = 0; i < model->outputs_len; i++) {
    float *output_buffer = NULL;
    OrtStatusPtr err = api->GetTensorMutableData(model->output_tensors[i],
                                                 (void **)&output_buffer);
    ONNX_CHECK_ERROR(err);
    printf("output %ld: %f\n", i, output_buffer[0]);
    outputs[i] = output_buffer[0];
  }
  return 0;
}