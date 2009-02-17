/**
 * collectd - src/java.c
 * Copyright (C) 2009  Florian octo Forster
 * Copyright (C) 2008  Justo Alonso Achaques
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; only version 2 of the License is applicable.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Authors:
 *   Florian octo Forster <octo at verplant.org>
 *   Justo Alonso Achaques <justo.alonso at gmail.com>
 **/

#include "collectd.h"
#include "plugin.h"
#include "common.h"

#include <pthread.h>
#include <jni.h>

#if !defined(JNI_VERSION_1_2)
# error "Need JNI 1.2 compatible interface!"
#endif

/*
 * Types
 */
struct java_plugin_s /* {{{ */
{
  char *class_name;
  jclass class_ptr;
  jobject object_ptr;

#define CJNI_FLAG_ENABLED 0x0001
  int flags;

  jmethodID method_init;
  jmethodID method_read;
  jmethodID method_shutdown;
};
typedef struct java_plugin_s java_plugin_t;
/* }}} */

/*
 * Global variables
 */
static JavaVM *jvm = NULL;

static java_plugin_t java_plugins[] =
{
  { "org.collectd.java.Foobar", NULL, NULL, 0, NULL, NULL, NULL }
};
static size_t java_plugins_num = sizeof (java_plugins) / sizeof (java_plugins[0]);

/* 
 * Conversion functons
 *
 * - jtoc_*: From Java to C
 * - ctoj_*: From C to Java
 */
static int jtoc_string (JNIEnv *jvm_env, /* {{{ */
    char *buffer, size_t buffer_size,
    jclass class_ptr, jobject object_ptr, const char *method_name)
{
  jmethodID method_id;
  jobject string_obj;
  const char *c_str;

  method_id = (*jvm_env)->GetMethodID (jvm_env, class_ptr,
      method_name, "()Ljava/lang/String;");
  if (method_id == NULL)
  {
    ERROR ("java plugin: jtoc_string: Cannot find method `String %s ()'.",
        method_name);
    return (-1);
  }

  string_obj = (*jvm_env)->CallObjectMethod (jvm_env, object_ptr, method_id);
  if (string_obj == NULL)
  {
    ERROR ("java plugin: jtoc_string: CallObjectMethod (%s) failed.",
        method_name);
    return (-1);
  }

  c_str = (*jvm_env)->GetStringUTFChars (jvm_env, string_obj, 0);
  if (c_str == NULL)
  {
    ERROR ("java plugin: jtoc_string: GetStringUTFChars failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, string_obj);
    return (-1);
  }

  DEBUG ("java plugin: jtoc_string: ->%s() = %s", method_name, c_str);

  sstrncpy (buffer, c_str, buffer_size);

  (*jvm_env)->ReleaseStringUTFChars (jvm_env, string_obj, c_str);
  (*jvm_env)->DeleteLocalRef (jvm_env, string_obj);

  return (0);
} /* }}} int jtoc_string */

static int jtoc_long (JNIEnv *jvm_env, /* {{{ */
    jlong *ret_value,
    jclass class_ptr, jobject object_ptr, const char *method_name)
{
  jmethodID method_id;

  method_id = (*jvm_env)->GetMethodID (jvm_env, class_ptr,
      method_name, "()J");
  if (method_id == NULL)
  {
    ERROR ("java plugin: jtoc_string: Cannot find method `long %s ()'.",
        method_name);
    return (-1);
  }

  *ret_value = (*jvm_env)->CallLongMethod (jvm_env, object_ptr, method_id);

  DEBUG ("java plugin: jtoc_long: ->%s() = %li",
      method_name, (long int) *ret_value);

  return (0);
} /* }}} int jtoc_long */

static int jtoc_double (JNIEnv *jvm_env, /* {{{ */
    jdouble *ret_value,
    jclass class_ptr, jobject object_ptr, const char *method_name)
{
  jmethodID method_id;

  method_id = (*jvm_env)->GetMethodID (jvm_env, class_ptr,
      method_name, "()D");
  if (method_id == NULL)
  {
    ERROR ("java plugin: jtoc_string: Cannot find method `double %s ()'.",
        method_name);
    return (-1);
  }

  *ret_value = (*jvm_env)->CallDoubleMethod (jvm_env, object_ptr, method_id);

  DEBUG ("java plugin: jtoc_double: ->%s() = %g",
      method_name, (double) *ret_value);

  return (0);
} /* }}} int jtoc_double */

static int jtoc_value (JNIEnv *jvm_env, /* {{{ */
    value_t *ret_value, int ds_type, jobject object_ptr)
{
  jclass class_ptr;
  int status;

  class_ptr = (*jvm_env)->GetObjectClass (jvm_env, object_ptr);

  if (ds_type == DS_TYPE_COUNTER)
  {
    jlong tmp_long;

    status = jtoc_long (jvm_env, &tmp_long,
        class_ptr, object_ptr, "longValue");
    if (status != 0)
    {
      ERROR ("java plugin: jtoc_value: "
          "jtoc_long failed.");
      return (-1);
    }
    (*ret_value).counter = (counter_t) tmp_long;
  }
  else
  {
    jdouble tmp_double;

    status = jtoc_double (jvm_env, &tmp_double,
        class_ptr, object_ptr, "doubleValue");
    if (status != 0)
    {
      ERROR ("java plugin: jtoc_value: "
          "jtoc_double failed.");
      return (-1);
    }
    (*ret_value).gauge = (gauge_t) tmp_double;
  }

  return (0);
} /* }}} int jtoc_value */

static int jtoc_values_array (JNIEnv *jvm_env, /* {{{ */
    const data_set_t *ds, value_list_t *vl,
    jclass class_ptr, jobject object_ptr)
{
  jmethodID m_getvalues;
  jmethodID m_toarray;
  jobject o_list;
  jobjectArray o_number_array;

  value_t *values;
  int values_num;
  int i;

  values_num = ds->ds_num;

  values = NULL;
  o_number_array = NULL;
  o_list = NULL;

#define BAIL_OUT(status) \
  free (values); \
  if (o_number_array != NULL) \
    (*jvm_env)->DeleteLocalRef (jvm_env, o_number_array); \
  if (o_list != NULL) \
    (*jvm_env)->DeleteLocalRef (jvm_env, o_list); \
  return (status);

  /* Call: List<Number> ValueList.getValues () */
  m_getvalues = (*jvm_env)->GetMethodID (jvm_env, class_ptr,
      "getValues", "()Ljava/util/List;");
  if (m_getvalues == NULL)
  {
    ERROR ("java plugin: jtoc_values_array: "
        "Cannot find method `List getValues ()'.");
    BAIL_OUT (-1);
  }

  o_list = (*jvm_env)->CallObjectMethod (jvm_env, object_ptr, m_getvalues);
  if (o_list == NULL)
  {
    ERROR ("java plugin: jtoc_values_array: "
        "CallObjectMethod (getValues) failed.");
    BAIL_OUT (-1);
  }

  /* Call: Number[] List.toArray () */
  m_toarray = (*jvm_env)->GetMethodID (jvm_env,
      (*jvm_env)->GetObjectClass (jvm_env, o_list),
      "toArray", "()[Ljava/lang/Object;");
  if (m_toarray == NULL)
  {
    ERROR ("java plugin: jtoc_values_array: "
        "Cannot find method `Object[] toArray ()'.");
    BAIL_OUT (-1);
  }

  o_number_array = (*jvm_env)->CallObjectMethod (jvm_env, o_list, m_toarray);
  if (o_number_array == NULL)
  {
    ERROR ("java plugin: jtoc_values_array: "
        "CallObjectMethod (toArray) failed.");
    BAIL_OUT (-1);
  }

  values = calloc (values_num, sizeof (value_t));
  if (values == NULL)
  {
    ERROR ("java plugin: jtoc_values_array: calloc failed.");
    BAIL_OUT (-1);
  }

  for (i = 0; i < values_num; i++)
  {
    jobject o_number;
    int status;

    o_number = (*jvm_env)->GetObjectArrayElement (jvm_env,
        o_number_array, (jsize) i);
    if (o_number == NULL)
    {
      ERROR ("java plugin: jtoc_values_array: "
          "GetObjectArrayElement (%i) failed.", i);
      BAIL_OUT (-1);
    }

    status = jtoc_value (jvm_env, values + i, ds->ds[i].type, o_number);
    if (status != 0)
    {
      ERROR ("java plugin: jtoc_values_array: "
          "jtoc_value (%i) failed.", i);
      BAIL_OUT (-1);
    }
  } /* for (i = 0; i < values_num; i++) */

  vl->values = values;
  vl->values_len = values_num;

#undef BAIL_OUT
  (*jvm_env)->DeleteLocalRef (jvm_env, o_number_array);
  (*jvm_env)->DeleteLocalRef (jvm_env, o_list);
  return (0);
} /* }}} int jtoc_values_array */

static int jtoc_value_list (JNIEnv *jvm_env, value_list_t *vl, /* {{{ */
    jobject object_ptr)
{
  jclass class_ptr;
  int status;
  jlong tmp_long;
  const data_set_t *ds;

  class_ptr = (*jvm_env)->GetObjectClass (jvm_env, object_ptr);
  if (class_ptr == NULL)
  {
    ERROR ("java plugin: jtoc_value_list: GetObjectClass failed.");
    return (-1);
  }

#define SET_STRING(buffer,method) do { \
  status = jtoc_string (jvm_env, buffer, sizeof (buffer), \
      class_ptr, object_ptr, method); \
  if (status != 0) { \
    ERROR ("java plugin: jtoc_value_list: jtoc_string (%s) failed.", \
        method); \
    return (-1); \
  } } while (0)

  SET_STRING(vl->type, "getType");

  ds = plugin_get_ds (vl->type);
  if (ds == NULL)
  {
    ERROR ("java plugin: jtoc_value_list: Data-set `%s' is not defined. "
        "Please consult the types.db(5) manpage for mor information.",
        vl->type);
    return (-1);
  }

  SET_STRING(vl->host, "getHost");
  SET_STRING(vl->plugin, "getPlugin");
  SET_STRING(vl->plugin_instance, "getPluginInstance");
  SET_STRING(vl->type_instance, "getTypeInstance");

#undef SET_STRING

  status = jtoc_long (jvm_env, &tmp_long, class_ptr, object_ptr, "getTime");
  if (status != 0)
  {
    ERROR ("java plugin: jtoc_value_list: jtoc_string (getTime) failed.");
    return (-1);
  }
  vl->time = (time_t) tmp_long;

  status = jtoc_long (jvm_env, &tmp_long,
      class_ptr, object_ptr, "getInterval");
  if (status != 0)
  {
    ERROR ("java plugin: jtoc_value_list: jtoc_string (getInterval) failed.");
    return (-1);
  }
  vl->interval = (int) tmp_long;

  status = jtoc_values_array (jvm_env, ds, vl, class_ptr, object_ptr);
  if (status != 0)
  {
    ERROR ("java plugin: jtoc_value_list: jtoc_values_array failed.");
    return (-1);
  }

  return (0);
} /* }}} int jtoc_value_list */

/* 
 * Functions accessible from Java
 */
static jint JNICALL cjni_api_dispatch_values (JNIEnv *jvm_env, /* {{{ */
    jobject this, jobject java_vl)
{
  value_list_t vl = VALUE_LIST_INIT;
  int status;

  DEBUG ("cjni_api_dispatch_values: java_vl = %p;", (void *) java_vl);

  status = jtoc_value_list (jvm_env, &vl, java_vl);
  if (status != 0)
  {
    ERROR ("java plugin: cjni_api_dispatch_values: jtoc_value_list failed.");
    return (-1);
  }

  plugin_dispatch_values (&vl);

  sfree (vl.values);

  return (0);
} /* }}} jint cjni_api_dispatch_values */

static JNINativeMethod jni_api_functions[] =
{
  { "DispatchValues", "(Lorg/collectd/protocol/ValueList;)I", cjni_api_dispatch_values }
};
static size_t jni_api_functions_num = sizeof (jni_api_functions)
  / sizeof (jni_api_functions[0]);

/*
 * Functions
 */
static int cjni_init_one_plugin (JNIEnv *jvm_env, java_plugin_t *jp) /* {{{ */
{
  jmethodID constructor_id;
  int status;

  jp->class_ptr = (*jvm_env)->FindClass (jvm_env, jp->class_name);
  if (jp->class_ptr == NULL)
  {
    ERROR ("cjni_init_one_plugin: FindClass (%s) failed.",
        jp->class_name);
    return (-1);
  }

  constructor_id = (*jvm_env)->GetMethodID (jvm_env, jp->class_ptr,
      "<init>", "()V");
  if (constructor_id == NULL)
  {
    ERROR ("cjni_init_one_plugin: Could not find the constructor for `%s'.",
        jp->class_name);
    return (-1);
  }

  jp->object_ptr = (*jvm_env)->NewObject (jvm_env, jp->class_ptr,
      constructor_id);
  if (jp->object_ptr == NULL)
  {
    ERROR ("cjni_init_one_plugin: Could create a new `%s' object.",
        jp->class_name);
    return (-1);
  }

  jp->method_init = (*jvm_env)->GetMethodID (jvm_env, jp->class_ptr,
      "Init", "()I");
  DEBUG ("jp->method_init = %p;", (void *) jp->method_init);
  jp->method_read = (*jvm_env)->GetMethodID (jvm_env, jp->class_ptr,
      "Read", "()I");
  DEBUG ("jp->method_read = %p;", (void *) jp->method_read);
  jp->method_shutdown = (*jvm_env)->GetMethodID (jvm_env, jp->class_ptr,
      "Shutdown", "()I");
  DEBUG ("jp->method_shutdown = %p;", (void *) jp->method_shutdown);

  status = (*jvm_env)->CallIntMethod (jvm_env, jp->object_ptr,
      jp->method_init);
  if (status != 0)
  {
    ERROR ("cjni_init_one_plugin: Initializing `%s' object failed "
        "with status %i.", jp->class_name, status);
    return (-1);
  }
  jp->flags |= CJNI_FLAG_ENABLED;

  return (0);
} /* }}} int cjni_init_one_plugin */

static int cjni_init_plugins (JNIEnv *jvm_env) /* {{{ */
{
  size_t j;

  for (j = 0; j < java_plugins_num; j++)
    cjni_init_one_plugin (jvm_env, &java_plugins[j]);

  return (0);
} /* }}} int cjni_init_plugins */

static int cjni_init_native (JNIEnv *jvm_env) /* {{{ */
{
  jclass api_class_ptr;
  int status;

  api_class_ptr = (*jvm_env)->FindClass (jvm_env, "org.collectd.java.CollectdAPI");
  if (api_class_ptr == NULL)
  {
    ERROR ("cjni_init_native: Cannot find API class `org.collectd.java.CollectdAPI'.");
    return (-1);
  }

  status = (*jvm_env)->RegisterNatives (jvm_env, api_class_ptr,
      jni_api_functions, (jint) jni_api_functions_num);
  if (status != 0)
  {
    ERROR ("cjni_init_native: RegisterNatives failed with status %i.", status);
    return (-1);
  }

  return (0);
} /* }}} int cjni_init_native */

static int cjni_init (void) /* {{{ */
{
  JNIEnv *jvm_env;
  JavaVMInitArgs vm_args;
  JavaVMOption vm_options[2];

  int status;

  if (jvm != NULL)
    return (0);

  jvm_env = NULL;

  memset (&vm_args, 0, sizeof (vm_args));
  vm_args.version = JNI_VERSION_1_2;
  vm_args.options = vm_options;
  vm_args.nOptions = sizeof (vm_options) / sizeof (vm_options[0]);

  vm_args.options[0].optionString = "-verbose:jni";
  vm_args.options[1].optionString = "-Djava.class.path=/home/octo/collectd/bindings/java";

  status = JNI_CreateJavaVM (&jvm, (void **) &jvm_env, (void **) &vm_args);
  if (status != 0)
  {
    ERROR ("cjni_init: JNI_CreateJavaVM failed with status %i.",
	status);
    return (-1);
  }
  assert (jvm != NULL);
  assert (jvm_env != NULL);

  /* Call RegisterNatives */
  status = cjni_init_native (jvm_env);
  if (status != 0)
  {
    ERROR ("cjni_init: cjni_init_native failed.");
    return (-1);
  }

  cjni_init_plugins (jvm_env);

  return (0);
} /* }}} int cjni_init */

static int cjni_read_one_plugin (JNIEnv *jvm_env, java_plugin_t *jp) /* {{{ */
{
  int status;

  if ((jp == NULL)
      || ((jp->flags & CJNI_FLAG_ENABLED) == 0)
      || (jp->method_read == NULL))
    return (0);

  DEBUG ("java plugin: Calling: %s.Read()", jp->class_name);

  status = (*jvm_env)->CallIntMethod (jvm_env, jp->object_ptr,
      jp->method_read);
  if (status != 0)
  {
    ERROR ("cjni_read_one_plugin: Calling `Read' on an `%s' object failed "
        "with status %i.", jp->class_name, status);
    return (-1);
  }

  return (0);
} /* }}} int cjni_read_one_plugin */

static int cjni_read_plugins (JNIEnv *jvm_env) /* {{{ */
{
  size_t j;

  for (j = 0; j < java_plugins_num; j++)
    cjni_read_one_plugin (jvm_env, &java_plugins[j]);

  return (0);
} /* }}} int cjni_read_plugins */

static int cjni_read (void) /* {{{ */
{
  JNIEnv *jvm_env;
  JavaVMAttachArgs args;
  int status;

  if (jvm == NULL)
  {
    ERROR ("java plugin: cjni_read: jvm == NULL");
    return (-1);
  }

  jvm_env = NULL;
  memset (&args, 0, sizeof (args));
  args.version = JNI_VERSION_1_2;

  status = (*jvm)->AttachCurrentThread (jvm, (void **) &jvm_env, &args);
  if (status != 0)
  {
    ERROR ("java plugin: cjni_read: AttachCurrentThread failed with status %i.",
        status);
    return (-1);
  }

  cjni_read_plugins (jvm_env);

  status = (*jvm)->DetachCurrentThread (jvm);
  if (status != 0)
  {
    ERROR ("java plugin: cjni_read: DetachCurrentThread failed with status %i.",
        status);
    return (-1);
  }

  return (0);
} /* }}} int cjni_read */

static int cjni_shutdown_one_plugin (JNIEnv *jvm_env, /* {{{ */
    java_plugin_t *jp)
{
  int status;

  if ((jp == NULL)
      || ((jp->flags & CJNI_FLAG_ENABLED) == 0)
      || (jp->method_shutdown == NULL))
    return (0);

  status = (*jvm_env)->CallIntMethod (jvm_env, jp->object_ptr,
      jp->method_shutdown);
  if (status != 0)
  {
    ERROR ("cjni_shutdown_one_plugin: Destroying an `%s' object failed "
        "with status %i.", jp->class_name, status);
    return (-1);
  }
  jp->flags &= ~CJNI_FLAG_ENABLED;

  return (0);
} /* }}} int cjni_shutdown_one_plugin */

static int cjni_shutdown_plugins (JNIEnv *jvm_env) /* {{{ */
{
  size_t j;

  for (j = 0; j < java_plugins_num; j++)
    cjni_shutdown_one_plugin (jvm_env, &java_plugins[j]);

  return (0);
} /* }}} int cjni_shutdown_plugins */

static int cjni_shutdown (void) /* {{{ */
{
  JNIEnv *jvm_env;
  JavaVMAttachArgs args;
  int status;

  if (jvm == NULL)
    return (0);

  jvm_env = NULL;
  memset (&args, 0, sizeof (args));
  args.version = JNI_VERSION_1_2;

  status = (*jvm)->AttachCurrentThread (jvm, (void **) &jvm_env, &args);
  if (status != 0)
  {
    ERROR ("java plugin: cjni_read: AttachCurrentThread failed with status %i.",
        status);
    return (-1);
  }

  cjni_shutdown_plugins (jvm_env);

  (*jvm)->DestroyJavaVM (jvm);
  jvm = NULL;
  jvm_env = NULL;

  return (0);
} /* }}} int cjni_shutdown */

void module_register (void)
{
  plugin_register_init ("java", cjni_init);
  plugin_register_read ("java", cjni_read);
  plugin_register_shutdown ("java", cjni_shutdown);
} /* void module_register (void) */

/* vim: set sw=2 sts=2 et fdm=marker : */
