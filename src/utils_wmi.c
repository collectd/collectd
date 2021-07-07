/*
 * collectd - src/utils_wmi.c
 * Copyright (c) 2018  Google LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */
#include "common.h"
#include "plugin.h"
#include "utils_wmi.h"

#include <stdio.h>
#include <stdlib.h>
#include <tchar.h>
#include <wbemidl.h>
#include <windows.h>
#include <wtypes.h>

#define log_err(...) ERROR("wmi_utils: " __VA_ARGS__)
#define log_warn(...) WARNING("wmi_utils: " __VA_ARGS__)

#define COUNTOF(x) (sizeof(x) / sizeof(x[0]))

static enum VARENUM variant_unsigned_integer_types[] = {VT_UI1, VT_UI2, VT_UI4,
                                                        VT_UI8, VT_UINT};

static int variant_is_unsigned_integer(VARIANT *v) {
  int i;
  for (i = 0; i < COUNTOF(variant_unsigned_integer_types); i++)
    if (v->vt == variant_unsigned_integer_types[i])
      return 1;
  return 0;
}

static enum VARENUM variant_signed_integer_types[] = {VT_I1, VT_I2, VT_I4,
                                                      VT_I8, VT_INT};

static int variant_is_signed_integer(VARIANT *v) {
  int i;
  for (i = 0; i < COUNTOF(variant_signed_integer_types); i++)
    if (v->vt == variant_signed_integer_types[i])
      return 1;
  return 0;
}

static char *varenum_to_string(const enum VARENUM v) {
  switch (v) {
  case VT_EMPTY:
    return "VT_EMPTY";
  case VT_NULL:
    return "VT_NULL";
  case VT_I2:
    return "VT_I2";
  case VT_I4:
    return "VT_I4";
  case VT_R4:
    return "VT_R4";
  case VT_R8:
    return "VT_R8";
  case VT_CY:
    return "VT_CY";
  case VT_DATE:
    return "VT_DATE";
  case VT_BSTR:
    return "VT_BSTR";
  case VT_DISPATCH:
    return "VT_DISPATCH";
  case VT_ERROR:
    return "VT_ERROR";
  case VT_BOOL:
    return "VT_BOOL";
  case VT_VARIANT:
    return "VT_VARIANT";
  case VT_UNKNOWN:
    return "VT_UNKNOWN";
  case VT_DECIMAL:
    return "VT_DECIMAL";
  case VT_I1:
    return "VT_I1";
  case VT_UI1:
    return "VT_UI1";
  case VT_UI2:
    return "VT_UI2";
  case VT_UI4:
    return "VT_UI4";
  case VT_I8:
    return "VT_I8";
  case VT_UI8:
    return "VT_UI8";
  case VT_INT:
    return "VT_INT";
  case VT_UINT:
    return "VT_UINT";
  case VT_VOID:
    return "VT_VOID";
  case VT_HRESULT:
    return "VT_HRESULT";
  case VT_PTR:
    return "VT_PTR";
  case VT_SAFEARRAY:
    return "VT_SAFEARRAY";
  case VT_CARRAY:
    return "VT_CARRAY";
  case VT_USERDEFINED:
    return "VT_USERDEFINED";
  case VT_LPSTR:
    return "VT_LPSTR";
  case VT_LPWSTR:
    return "VT_LPWSTR";
  case VT_RECORD:
    return "VT_RECORD";
  case VT_INT_PTR:
    return "VT_INT_PTR";
  case VT_UINT_PTR:
    return "VT_UINT_PTR";
  case VT_FILETIME:
    return "VT_FILETIME";
  case VT_BLOB:
    return "VT_BLOB";
  case VT_STREAM:
    return "VT_STREAM";
  case VT_STORAGE:
    return "VT_STORAGE";
  case VT_STREAMED_OBJECT:
    return "VT_STREAMED_OBJECT";
  case VT_STORED_OBJECT:
    return "VT_STORED_OBJECT";
  case VT_BLOB_OBJECT:
    return "VT_BLOB_OBJECT";
  case VT_CF:
    return "VT_CF";
  case VT_CLSID:
    return "VT_CLSID";
  case VT_VERSIONED_STREAM:
    return "VT_VERSIONED_STREAM";
  case VT_BSTR_BLOB:
    return "VT_BSTR_BLOB";
  case VT_VECTOR:
    return "VT_VECTOR";
  case VT_ARRAY:
    return "VT_ARRAY";
  case VT_BYREF:
    return "VT_BYREF";
  case VT_RESERVED:
    return "VT_RESERVED";
  case VT_ILLEGAL:
    return "VT_ILLEGAL";
  default:
    return "<unknown>";
  }
}

static uint64_t variant_get_unsigned_integer(VARIANT *v) {
  switch (v->vt) {
  case VT_UI1:
    return v->bVal;
  case VT_UI2:
    return v->uiVal;
  case VT_UI4:
    return v->ulVal;
  case VT_UI8:
    return v->ullVal;
  case VT_UINT:
    return v->uintVal;
  default:
    log_err("cannot convert from type %s (%d) to uint64_t",
            varenum_to_string(v->vt), v->vt);
    return 0;
  }
}

static int64_t variant_get_signed_integer(VARIANT *v) {
  switch (v->vt) {
  case VT_I1:
    return v->bVal;
  case VT_I2:
    return v->iVal;
  case VT_I4:
    return v->lVal;
  case VT_I8:
    return v->llVal;
  case VT_INT:
    return v->intVal;
  default:
    log_err("cannot convert from type %s (%d) to int64_t",
            varenum_to_string(v->vt), v->vt);
    return 0;
  }
}

static int variant_is_real(VARIANT *v) {
  return v->vt == VT_R4 || v->vt == VT_R8;
}

static double variant_get_real(VARIANT *v) {
  switch (v->vt) {
  case VT_R4:
    return v->fltVal;
  case VT_R8:
    return v->dblVal;
  default:
    log_err("cannot convert from type %s (%d) to int", varenum_to_string(v->vt),
            v->vt);
    return 0;
  }
}

int64_t variant_get_int64(VARIANT *v) {
  int64_t result = 0;

  if (variant_is_unsigned_integer(v))
    return variant_get_unsigned_integer(v);
  else if (variant_is_signed_integer(v))
    return variant_get_signed_integer(v);
  else if (variant_is_real(v))
    return variant_get_real(v);
  else if (v->vt == VT_BSTR) {
    char *str = wstrtostr(v->bstrVal);
    int status = sscanf(str, "%" SCNd64, &result);
    if (status <= 0)
      log_err("cannot convert '%s' to int64.", str);
    free(str);
    return result;
  } else {
    log_err("cannot convert from type %s (%d) to int64_t",
            varenum_to_string(v->vt), v->vt);
    return 0;
  }
}

uint64_t variant_get_uint64(VARIANT *v) {
  uint64_t result = 0;

  if (variant_is_unsigned_integer(v))
    return variant_get_unsigned_integer(v);
  else if (variant_is_signed_integer(v))
    return variant_get_signed_integer(v);
  else if (variant_is_real(v))
    return variant_get_real(v);
  else if (v->vt == VT_BSTR) {
    char *str = wstrtostr(v->bstrVal);
    int status = sscanf(str, "%" SCNu64, &result);
    if (status <= 0)
      log_err("cannot convert '%s' to uint64.", str);
    free(str);

    return result;
  } else {
    log_err("cannot convert from type %s (%d) to uint64_t",
            varenum_to_string(v->vt), v->vt);
    return 0;
  }
}

double variant_get_double(VARIANT *v) {
  double result = 0;

  if (variant_is_unsigned_integer(v))
    return variant_get_unsigned_integer(v);
  else if (variant_is_signed_integer(v))
    return variant_get_signed_integer(v);
  else if (variant_is_real(v))
    return variant_get_real(v);
  else if (v->vt == VT_BSTR) {
    char *str = wstrtostr(v->bstrVal);
    int status = sscanf(str, "%lf", &result);
    if (status <= 0)
      log_err("cannot convert '%s' to double.", str);
    free(str);

    return result;
  } else {
    log_err("cannot convert from type %s (%d) to double",
            varenum_to_string(v->vt), v->vt);
    return 0;
  }
}

char *variant_get_string(VARIANT *v) {
  if (v->vt != VT_BSTR) {
    log_err("cannot convert from type %s (%d) to string",
            varenum_to_string(v->vt), v->vt);
    return NULL;
  }
  return wstrtostr(v->bstrVal);
}

/* String conversion utils */
wchar_t *strtowstr(const char *source) {
  int source_len;
  wchar_t *result;

  source_len = strlen(source);
  result = calloc(source_len + 1, sizeof(wchar_t));
  mbstowcs(result, source, source_len + 1);

  return result;
}

char *wstrtostr(const wchar_t *source) {
  int source_len;
  char *result;

  source_len = wcslen(source);
  result = calloc(source_len + 1, sizeof(char));
  wcstombs(result, source, source_len + 1);
  return result;
}

static HRESULT wmi_make_call(IDispatch *dispatcher, const wchar_t *name_str,
                             DISPPARAMS *params, VARIANT *result, WORD flags) {
  HRESULT hr;
  DISPID dispid[1];
  BSTR name;

  name = SysAllocString(name_str);
  hr = dispatcher->lpVtbl->GetIDsOfNames(dispatcher, &IID_NULL, &name, 1,
                                         LOCALE_SYSTEM_DEFAULT, dispid);
  SysFreeString(name);

  if (FAILED(hr))
    return hr;

  hr = dispatcher->lpVtbl->Invoke(dispatcher, dispid[0], &IID_NULL,
                                  LOCALE_SYSTEM_DEFAULT, flags, params, result,
                                  NULL, NULL);
  if (FAILED(hr))
    return hr;

  return 0;
}

HRESULT wmi_invoke_method(IDispatch *dispatcher, const wchar_t *method_name,
                          DISPPARAMS *params, VARIANT *result) {
  return wmi_make_call(dispatcher, method_name, params, result,
                       DISPATCH_METHOD);
}

HRESULT wmi_get_property(IDispatch *dispatcher, const wchar_t *property_name,
                         VARIANT *result) {
  DISPPARAMS params = {
      .cArgs = 0,
      .cNamedArgs = 0,
  };
  return wmi_make_call(dispatcher, property_name, &params, result,
                       DISPATCH_PROPERTYGET);
}

wmi_result_list_t *wmi_query(wmi_connection_t *connection, const char *query) {
  HRESULT status = 0;
  wmi_result_list_t *res = NULL;

  VARIANT methodResult;
  VARIANT propertyResult;
  VARIANTARG args[1];
  DISPPARAMS params;

  params.cNamedArgs = 0;
  params.rgvarg = args;

  wchar_t *wquery = strtowstr(query);
  params.cArgs = 1;
  params.rgvarg[0].vt = VT_BSTR;
  params.rgvarg[0].bstrVal = SysAllocString(wquery);
  free(wquery);

  status = wmi_invoke_method(connection->dispatcher, L"ExecQuery", &params,
                             &methodResult);
  SysFreeString(params.rgvarg[0].bstrVal);
  VariantClear(args);
  if (FAILED(status)) {
    VariantClear(&methodResult);
    log_err("unknown error [0x%x] during query: '%s'. Error details: "
            "ExecQuery() failed.",
            (unsigned)status, query);
    return NULL;
  }

  res = malloc(sizeof(wmi_result_list_t));
  res->results = methodResult.pdispVal;
  methodResult.pdispVal = NULL;
  VariantClear(&methodResult);

  status = wmi_get_property(res->results, L"Count", &propertyResult);
  if (FAILED(status)) {
    // It is very likely that the set returned by ExecQuery is empty,
    // which seems to be signaled by a missing 'Count' property.
    // Because of that, we're not going to treat it as an error.
    res->count = 0;
  } else {
    res->count = propertyResult.intVal;
  }
  res->last_result = -1;
  VariantClear(&propertyResult);

  return res;
}

void wmi_result_list_release(wmi_result_list_t *results) {
  if (results) {
    results->results->lpVtbl->Release(results->results);
    free(results);
  }
}

wmi_result_t *wmi_get_next_result(wmi_result_list_t *results) {
  if (!results)
    return NULL;

  if (results->last_result + 1 >= results->count)
    return NULL;

  HRESULT hr;
  VARIANTARG args[1] = {{
      .vt = VT_UI4,
      .uintVal = results->last_result + 1,
  }};
  DISPPARAMS params = {
      .cArgs = 1,
      .cNamedArgs = 0,
      .rgvarg = args,
  };
  VARIANT varResult;

  hr = wmi_invoke_method(results->results, L"ItemIndex", &params, &varResult);
  if (FAILED(hr)) {
    VariantClear(&varResult);
    VariantClear(args);
    log_err("cannot get next result. Error code 0x%x", (unsigned)hr);
    return NULL;
  }

  results->last_result++;

  wmi_result_t *result = malloc(sizeof(wmi_result_t));
  result->result = varResult.pdispVal;
  varResult.pdispVal = NULL;
  VariantClear(&varResult);
  VariantClear(args);
  return result;
}

void wmi_result_release(wmi_result_t *result) {
  if (result) {
    result->result->lpVtbl->Release(result->result);
    free(result);
  }
}

static void log_hresult_error(HRESULT hr, const char *property_name) {
  switch (hr) {
  case WBEM_E_NOT_FOUND:
    log_err("property %s not found.", property_name);
    break;
  default:
    log_err("unknown error 0x%x while fetching property %s", (unsigned)hr,
            property_name);
    break;
  }
}

int wmi_result_get_value(const wmi_result_t *result, const char *name,
                         VARIANT *value) {
  VARIANT propertyResult;
  VARIANT methodResult;
  HRESULT hr;

  hr = wmi_get_property(result->result, L"Properties_", &propertyResult);
  if (FAILED(hr)) {
    VariantClear(&propertyResult);
    log_hresult_error(hr, name);
    return -1;
  }

  wchar_t *wname = strtowstr(name);
  VARIANTARG args[1] = {{
      .vt = VT_BSTR,
      .bstrVal = SysAllocString(wname),
  }};
  free(wname);
  DISPPARAMS params = {
      .cArgs = 1,
      .cNamedArgs = 0,
      .rgvarg = args,
  };

  hr = wmi_invoke_method(propertyResult.pdispVal, L"Item", &params,
                         &methodResult);
  VariantClear(&propertyResult);
  SysFreeString(params.rgvarg[0].bstrVal);
  VariantClear(args);
  if (FAILED(hr)) {
    VariantClear(&methodResult);
    log_hresult_error(hr, name);
    return -1;
  }
  hr = wmi_get_property(methodResult.pdispVal, L"Value", value);
  VariantClear(&methodResult);
  if (FAILED(hr)) {
    log_hresult_error(hr, name);
    return -1;
  }

  return 0;
}

void wmi_release(wmi_connection_t *connection) {
  if (!connection)
    return;

  if (connection->dispatcher)
    connection->dispatcher->lpVtbl->Release(connection->dispatcher);

  CoUninitialize();
  free(connection);
}

wmi_connection_t *wmi_connect(void) {
  wmi_connection_t *connection = malloc(sizeof(wmi_connection_t));
  HRESULT hr = CoInitializeEx(0, COINIT_MULTITHREADED);
  if (FAILED(hr)) {
    log_err("initialization failed. Error code: %x", (unsigned)hr);
    wmi_release(connection);
    return NULL;
  }

  BSTR str = SysAllocString(L"winmgmts:root\\cimv2");
  hr = CoGetObject(str, NULL, &IID_IDispatch, (void **)&connection->dispatcher);
  SysFreeString(str);
  if (FAILED(hr)) {
    log_err("initialization failed. Error code: %x", (unsigned)hr);
    wmi_release(connection);
    return NULL;
  }

  return connection;
}
