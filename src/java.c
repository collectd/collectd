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
#include "filter_chain.h"

#include <pthread.h>
#include <jni.h>

#if !defined(JNI_VERSION_1_2)
# error "Need JNI 1.2 compatible interface!"
#endif

/*
 * Types
 */
struct cjni_jvm_env_s /* {{{ */
{
  JNIEnv *jvm_env;
  int reference_counter;
};
typedef struct cjni_jvm_env_s cjni_jvm_env_t;
/* }}} */

struct java_plugin_class_s /* {{{ */
{
  char     *name;
  jclass    class;
  jobject   object;
};
typedef struct java_plugin_class_s java_plugin_class_t;
/* }}} */

#define CB_TYPE_CONFIG       1
#define CB_TYPE_INIT         2
#define CB_TYPE_READ         3
#define CB_TYPE_WRITE        4
#define CB_TYPE_FLUSH        5
#define CB_TYPE_SHUTDOWN     6
#define CB_TYPE_LOG          7
#define CB_TYPE_NOTIFICATION 8
#define CB_TYPE_MATCH        9
#define CB_TYPE_TARGET      10
struct cjni_callback_info_s /* {{{ */
{
  char     *name;
  int       type;
  jclass    class;
  jobject   object;
  jmethodID method;
};
typedef struct cjni_callback_info_s cjni_callback_info_t;
/* }}} */

/*
 * Global variables
 */
static JavaVM *jvm = NULL;
static pthread_key_t jvm_env_key;

/* Configuration options for the JVM. */
static char **jvm_argv = NULL;
static size_t jvm_argc = 0;

/* List of class names to load */
static java_plugin_class_t  *java_classes_list = NULL;
static size_t                java_classes_list_len;

/* List of config, init, and shutdown callbacks. */
static cjni_callback_info_t *java_callbacks      = NULL;
static size_t                java_callbacks_num  = 0;
static pthread_mutex_t       java_callbacks_lock = PTHREAD_MUTEX_INITIALIZER;

static oconfig_item_t       *config_block = NULL;

/*
 * Prototypes
 *
 * Mostly functions that are needed by the Java interface (``native'')
 * functions.
 */
static void cjni_callback_info_destroy (void *arg);
static cjni_callback_info_t *cjni_callback_info_create (JNIEnv *jvm_env,
    jobject o_name, jobject o_callback, int type);
static int cjni_callback_register (JNIEnv *jvm_env, jobject o_name,
    jobject o_callback, int type);
static int cjni_read (user_data_t *user_data);
static int cjni_write (const data_set_t *ds, const value_list_t *vl,
    user_data_t *ud);
static int cjni_flush (cdtime_t timeout, const char *identifier, user_data_t *ud);
static void cjni_log (int severity, const char *message, user_data_t *ud);
static int cjni_notification (const notification_t *n, user_data_t *ud);

/* Create, destroy, and match/invoke functions, used by both, matches AND
 * targets. */
static int cjni_match_target_create (const oconfig_item_t *ci, void **user_data);
static int cjni_match_target_destroy (void **user_data);
static int cjni_match_target_invoke (const data_set_t *ds, value_list_t *vl,
    notification_meta_t **meta, void **user_data);

/* 
 * C to Java conversion functions
 */
static int ctoj_string (JNIEnv *jvm_env, /* {{{ */
    const char *string,
    jclass class_ptr, jobject object_ptr, const char *method_name)
{
  jmethodID m_set;
  jstring o_string;

  /* Create a java.lang.String */
  o_string = (*jvm_env)->NewStringUTF (jvm_env,
      (string != NULL) ? string : "");
  if (o_string == NULL)
  {
    ERROR ("java plugin: ctoj_string: NewStringUTF failed.");
    return (-1);
  }

  /* Search for the `void setFoo (String s)' method. */
  m_set = (*jvm_env)->GetMethodID (jvm_env, class_ptr,
      method_name, "(Ljava/lang/String;)V");
  if (m_set == NULL)
  {
    ERROR ("java plugin: ctoj_string: Cannot find method `void %s (String)'.",
        method_name);
    (*jvm_env)->DeleteLocalRef (jvm_env, o_string);
    return (-1);
  }

  /* Call the method. */
  (*jvm_env)->CallVoidMethod (jvm_env, object_ptr, m_set, o_string);

  /* Decrease reference counter on the java.lang.String object. */
  (*jvm_env)->DeleteLocalRef (jvm_env, o_string);

  return (0);
} /* }}} int ctoj_string */

static int ctoj_int (JNIEnv *jvm_env, /* {{{ */
    jint value,
    jclass class_ptr, jobject object_ptr, const char *method_name)
{
  jmethodID m_set;

  /* Search for the `void setFoo (int i)' method. */
  m_set = (*jvm_env)->GetMethodID (jvm_env, class_ptr,
      method_name, "(I)V");
  if (m_set == NULL)
  {
    ERROR ("java plugin: ctoj_int: Cannot find method `void %s (int)'.",
        method_name);
    return (-1);
  }

  (*jvm_env)->CallVoidMethod (jvm_env, object_ptr, m_set, value);

  return (0);
} /* }}} int ctoj_int */

static int ctoj_long (JNIEnv *jvm_env, /* {{{ */
    jlong value,
    jclass class_ptr, jobject object_ptr, const char *method_name)
{
  jmethodID m_set;

  /* Search for the `void setFoo (long l)' method. */
  m_set = (*jvm_env)->GetMethodID (jvm_env, class_ptr,
      method_name, "(J)V");
  if (m_set == NULL)
  {
    ERROR ("java plugin: ctoj_long: Cannot find method `void %s (long)'.",
        method_name);
    return (-1);
  }

  (*jvm_env)->CallVoidMethod (jvm_env, object_ptr, m_set, value);

  return (0);
} /* }}} int ctoj_long */

static int ctoj_double (JNIEnv *jvm_env, /* {{{ */
    jdouble value,
    jclass class_ptr, jobject object_ptr, const char *method_name)
{
  jmethodID m_set;

  /* Search for the `void setFoo (double d)' method. */
  m_set = (*jvm_env)->GetMethodID (jvm_env, class_ptr,
      method_name, "(D)V");
  if (m_set == NULL)
  {
    ERROR ("java plugin: ctoj_double: Cannot find method `void %s (double)'.",
        method_name);
    return (-1);
  }

  (*jvm_env)->CallVoidMethod (jvm_env, object_ptr, m_set, value);

  return (0);
} /* }}} int ctoj_double */

/* Convert a jlong to a java.lang.Number */
static jobject ctoj_jlong_to_number (JNIEnv *jvm_env, jlong value) /* {{{ */
{
  jclass c_long;
  jmethodID m_long_constructor;

  /* Look up the java.lang.Long class */
  c_long = (*jvm_env)->FindClass (jvm_env, "java/lang/Long");
  if (c_long == NULL)
  {
    ERROR ("java plugin: ctoj_jlong_to_number: Looking up the "
        "java.lang.Long class failed.");
    return (NULL);
  }

  m_long_constructor = (*jvm_env)->GetMethodID (jvm_env,
      c_long, "<init>", "(J)V");
  if (m_long_constructor == NULL)
  {
    ERROR ("java plugin: ctoj_jlong_to_number: Looking up the "
        "`Long (long)' constructor failed.");
    return (NULL);
  }

  return ((*jvm_env)->NewObject (jvm_env,
        c_long, m_long_constructor, value));
} /* }}} jobject ctoj_jlong_to_number */

/* Convert a jdouble to a java.lang.Number */
static jobject ctoj_jdouble_to_number (JNIEnv *jvm_env, jdouble value) /* {{{ */
{
  jclass c_double;
  jmethodID m_double_constructor;

  /* Look up the java.lang.Long class */
  c_double = (*jvm_env)->FindClass (jvm_env, "java/lang/Double");
  if (c_double == NULL)
  {
    ERROR ("java plugin: ctoj_jdouble_to_number: Looking up the "
        "java.lang.Double class failed.");
    return (NULL);
  }

  m_double_constructor = (*jvm_env)->GetMethodID (jvm_env,
      c_double, "<init>", "(D)V");
  if (m_double_constructor == NULL)
  {
    ERROR ("java plugin: ctoj_jdouble_to_number: Looking up the "
        "`Double (double)' constructor failed.");
    return (NULL);
  }

  return ((*jvm_env)->NewObject (jvm_env,
        c_double, m_double_constructor, value));
} /* }}} jobject ctoj_jdouble_to_number */

/* Convert a value_t to a java.lang.Number */
static jobject ctoj_value_to_number (JNIEnv *jvm_env, /* {{{ */
    value_t value, int ds_type)
{
  if (ds_type == DS_TYPE_COUNTER)
    return (ctoj_jlong_to_number (jvm_env, (jlong) value.counter));
  else if (ds_type == DS_TYPE_GAUGE)
    return (ctoj_jdouble_to_number (jvm_env, (jdouble) value.gauge));
  if (ds_type == DS_TYPE_DERIVE)
    return (ctoj_jlong_to_number (jvm_env, (jlong) value.derive));
  if (ds_type == DS_TYPE_ABSOLUTE)
    return (ctoj_jlong_to_number (jvm_env, (jlong) value.absolute));
  else
    return (NULL);
} /* }}} jobject ctoj_value_to_number */

/* Convert a data_source_t to a org/collectd/api/DataSource */
static jobject ctoj_data_source (JNIEnv *jvm_env, /* {{{ */
    const data_source_t *dsrc)
{
  jclass c_datasource;
  jmethodID m_datasource_constructor;
  jobject o_datasource;
  int status;

  /* Look up the DataSource class */
  c_datasource = (*jvm_env)->FindClass (jvm_env,
      "org/collectd/api/DataSource");
  if (c_datasource == NULL)
  {
    ERROR ("java plugin: ctoj_data_source: "
        "FindClass (org/collectd/api/DataSource) failed.");
    return (NULL);
  }

  /* Lookup the `ValueList ()' constructor. */
  m_datasource_constructor = (*jvm_env)->GetMethodID (jvm_env, c_datasource,
      "<init>", "()V");
  if (m_datasource_constructor == NULL)
  {
    ERROR ("java plugin: ctoj_data_source: Cannot find the "
        "`DataSource ()' constructor.");
    return (NULL);
  }

  /* Create a new instance. */
  o_datasource = (*jvm_env)->NewObject (jvm_env, c_datasource,
      m_datasource_constructor);
  if (o_datasource == NULL)
  {
    ERROR ("java plugin: ctoj_data_source: "
        "Creating a new DataSource instance failed.");
    return (NULL);
  }

  /* Set name via `void setName (String name)' */
  status = ctoj_string (jvm_env, dsrc->name,
      c_datasource, o_datasource, "setName");
  if (status != 0)
  {
    ERROR ("java plugin: ctoj_data_source: "
        "ctoj_string (setName) failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, o_datasource);
    return (NULL);
  }

  /* Set type via `void setType (int type)' */
  status = ctoj_int (jvm_env, dsrc->type,
      c_datasource, o_datasource, "setType");
  if (status != 0)
  {
    ERROR ("java plugin: ctoj_data_source: "
        "ctoj_int (setType) failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, o_datasource);
    return (NULL);
  }

  /* Set min via `void setMin (double min)' */
  status = ctoj_double (jvm_env, dsrc->min,
      c_datasource, o_datasource, "setMin");
  if (status != 0)
  {
    ERROR ("java plugin: ctoj_data_source: "
        "ctoj_double (setMin) failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, o_datasource);
    return (NULL);
  }

  /* Set max via `void setMax (double max)' */
  status = ctoj_double (jvm_env, dsrc->max,
      c_datasource, o_datasource, "setMax");
  if (status != 0)
  {
    ERROR ("java plugin: ctoj_data_source: "
        "ctoj_double (setMax) failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, o_datasource);
    return (NULL);
  }

  return (o_datasource);
} /* }}} jobject ctoj_data_source */

/* Convert a oconfig_value_t to a org/collectd/api/OConfigValue */
static jobject ctoj_oconfig_value (JNIEnv *jvm_env, /* {{{ */
    oconfig_value_t ocvalue)
{
  jclass c_ocvalue;
  jmethodID m_ocvalue_constructor;
  jobject o_argument;
  jobject o_ocvalue;

  m_ocvalue_constructor = NULL;
  o_argument = NULL;

  c_ocvalue = (*jvm_env)->FindClass (jvm_env,
      "org/collectd/api/OConfigValue");
  if (c_ocvalue == NULL)
  {
    ERROR ("java plugin: ctoj_oconfig_value: "
        "FindClass (org/collectd/api/OConfigValue) failed.");
    return (NULL);
  }

  if (ocvalue.type == OCONFIG_TYPE_BOOLEAN)
  {
    jboolean tmp_boolean;

    tmp_boolean = (ocvalue.value.boolean == 0) ? JNI_FALSE : JNI_TRUE;

    m_ocvalue_constructor = (*jvm_env)->GetMethodID (jvm_env, c_ocvalue,
        "<init>", "(Z)V");
    if (m_ocvalue_constructor == NULL)
    {
      ERROR ("java plugin: ctoj_oconfig_value: Cannot find the "
          "`OConfigValue (boolean)' constructor.");
      return (NULL);
    }

    return ((*jvm_env)->NewObject (jvm_env,
          c_ocvalue, m_ocvalue_constructor, tmp_boolean));
  } /* if (ocvalue.type == OCONFIG_TYPE_BOOLEAN) */
  else if (ocvalue.type == OCONFIG_TYPE_STRING)
  {
    m_ocvalue_constructor = (*jvm_env)->GetMethodID (jvm_env, c_ocvalue,
        "<init>", "(Ljava/lang/String;)V");
    if (m_ocvalue_constructor == NULL)
    {
      ERROR ("java plugin: ctoj_oconfig_value: Cannot find the "
          "`OConfigValue (String)' constructor.");
      return (NULL);
    }

    o_argument = (*jvm_env)->NewStringUTF (jvm_env, ocvalue.value.string);
    if (o_argument == NULL)
    {
      ERROR ("java plugin: ctoj_oconfig_value: "
          "Creating a String object failed.");
      return (NULL);
    }
  }
  else if (ocvalue.type == OCONFIG_TYPE_NUMBER)
  {
    m_ocvalue_constructor = (*jvm_env)->GetMethodID (jvm_env, c_ocvalue,
        "<init>", "(Ljava/lang/Number;)V");
    if (m_ocvalue_constructor == NULL)
    {
      ERROR ("java plugin: ctoj_oconfig_value: Cannot find the "
          "`OConfigValue (Number)' constructor.");
      return (NULL);
    }

    o_argument = ctoj_jdouble_to_number (jvm_env,
        (jdouble) ocvalue.value.number);
    if (o_argument == NULL)
    {
      ERROR ("java plugin: ctoj_oconfig_value: "
          "Creating a Number object failed.");
      return (NULL);
    }
  }
  else
  {
    return (NULL);
  }

  assert (m_ocvalue_constructor != NULL);
  assert (o_argument != NULL);

  o_ocvalue = (*jvm_env)->NewObject (jvm_env,
      c_ocvalue, m_ocvalue_constructor, o_argument);
  if (o_ocvalue == NULL)
  {
    ERROR ("java plugin: ctoj_oconfig_value: "
        "Creating an OConfigValue object failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, o_argument);
    return (NULL);
  }

  (*jvm_env)->DeleteLocalRef (jvm_env, o_argument);
  return (o_ocvalue);
} /* }}} jobject ctoj_oconfig_value */

/* Convert a oconfig_item_t to a org/collectd/api/OConfigItem */
static jobject ctoj_oconfig_item (JNIEnv *jvm_env, /* {{{ */
    const oconfig_item_t *ci)
{
  jclass c_ocitem;
  jmethodID m_ocitem_constructor;
  jmethodID m_addvalue;
  jmethodID m_addchild;
  jobject o_key;
  jobject o_ocitem;
  int i;

  c_ocitem = (*jvm_env)->FindClass (jvm_env, "org/collectd/api/OConfigItem");
  if (c_ocitem == NULL)
  {
    ERROR ("java plugin: ctoj_oconfig_item: "
        "FindClass (org/collectd/api/OConfigItem) failed.");
    return (NULL);
  }

  /* Get the required methods: m_ocitem_constructor, m_addvalue, and m_addchild
   * {{{ */
  m_ocitem_constructor = (*jvm_env)->GetMethodID (jvm_env, c_ocitem,
      "<init>", "(Ljava/lang/String;)V");
  if (m_ocitem_constructor == NULL)
  {
    ERROR ("java plugin: ctoj_oconfig_item: Cannot find the "
        "`OConfigItem (String)' constructor.");
    return (NULL);
  }

  m_addvalue = (*jvm_env)->GetMethodID (jvm_env, c_ocitem,
      "addValue", "(Lorg/collectd/api/OConfigValue;)V");
  if (m_addvalue == NULL)
  {
    ERROR ("java plugin: ctoj_oconfig_item: Cannot find the "
        "`addValue (OConfigValue)' method.");
    return (NULL);
  }

  m_addchild = (*jvm_env)->GetMethodID (jvm_env, c_ocitem,
      "addChild", "(Lorg/collectd/api/OConfigItem;)V");
  if (m_addchild == NULL)
  {
    ERROR ("java plugin: ctoj_oconfig_item: Cannot find the "
        "`addChild (OConfigItem)' method.");
    return (NULL);
  }
  /* }}} */

  /* Create a String object with the key.
   * Needed for calling the constructor. */
  o_key = (*jvm_env)->NewStringUTF (jvm_env, ci->key);
  if (o_key == NULL)
  {
    ERROR ("java plugin: ctoj_oconfig_item: "
        "Creating String object failed.");
    return (NULL);
  }

  /* Create an OConfigItem object */
  o_ocitem = (*jvm_env)->NewObject (jvm_env,
      c_ocitem, m_ocitem_constructor, o_key);
  if (o_ocitem == NULL)
  {
    ERROR ("java plugin: ctoj_oconfig_item: "
        "Creating an OConfigItem object failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, o_key);
    return (NULL);
  }

  /* We don't need the String object any longer.. */
  (*jvm_env)->DeleteLocalRef (jvm_env, o_key);

  /* Call OConfigItem.addValue for each value */
  for (i = 0; i < ci->values_num; i++) /* {{{ */
  {
    jobject o_value;

    o_value = ctoj_oconfig_value (jvm_env, ci->values[i]);
    if (o_value == NULL)
    {
      ERROR ("java plugin: ctoj_oconfig_item: "
          "Creating an OConfigValue object failed.");
      (*jvm_env)->DeleteLocalRef (jvm_env, o_ocitem);
      return (NULL);
    }

    (*jvm_env)->CallVoidMethod (jvm_env, o_ocitem, m_addvalue, o_value);
    (*jvm_env)->DeleteLocalRef (jvm_env, o_value);
  } /* }}} for (i = 0; i < ci->values_num; i++) */

  /* Call OConfigItem.addChild for each child */
  for (i = 0; i < ci->children_num; i++) /* {{{ */
  {
    jobject o_child;

    o_child = ctoj_oconfig_item (jvm_env, ci->children + i);
    if (o_child == NULL)
    {
      ERROR ("java plugin: ctoj_oconfig_item: "
          "Creating an OConfigItem object failed.");
      (*jvm_env)->DeleteLocalRef (jvm_env, o_ocitem);
      return (NULL);
    }

    (*jvm_env)->CallVoidMethod (jvm_env, o_ocitem, m_addchild, o_child);
    (*jvm_env)->DeleteLocalRef (jvm_env, o_child);
  } /* }}} for (i = 0; i < ci->children_num; i++) */

  return (o_ocitem);
} /* }}} jobject ctoj_oconfig_item */

/* Convert a data_set_t to a org/collectd/api/DataSet */
static jobject ctoj_data_set (JNIEnv *jvm_env, const data_set_t *ds) /* {{{ */
{
  jclass c_dataset;
  jmethodID m_constructor;
  jmethodID m_add;
  jobject o_type;
  jobject o_dataset;
  int i;

  /* Look up the org/collectd/api/DataSet class */
  c_dataset = (*jvm_env)->FindClass (jvm_env, "org/collectd/api/DataSet");
  if (c_dataset == NULL)
  {
    ERROR ("java plugin: ctoj_data_set: Looking up the "
        "org/collectd/api/DataSet class failed.");
    return (NULL);
  }

  /* Search for the `DataSet (String type)' constructor. */
  m_constructor = (*jvm_env)->GetMethodID (jvm_env,
      c_dataset, "<init>", "(Ljava/lang/String;)V");
  if (m_constructor == NULL)
  {
    ERROR ("java plugin: ctoj_data_set: Looking up the "
        "`DataSet (String)' constructor failed.");
    return (NULL);
  }

  /* Search for the `void addDataSource (DataSource)' method. */
  m_add = (*jvm_env)->GetMethodID (jvm_env,
      c_dataset, "addDataSource", "(Lorg/collectd/api/DataSource;)V");
  if (m_add == NULL)
  {
    ERROR ("java plugin: ctoj_data_set: Looking up the "
        "`addDataSource (DataSource)' method failed.");
    return (NULL);
  }

  o_type = (*jvm_env)->NewStringUTF (jvm_env, ds->type);
  if (o_type == NULL)
  {
    ERROR ("java plugin: ctoj_data_set: Creating a String object failed.");
    return (NULL);
  }

  o_dataset = (*jvm_env)->NewObject (jvm_env,
      c_dataset, m_constructor, o_type);
  if (o_dataset == NULL)
  {
    ERROR ("java plugin: ctoj_data_set: Creating a DataSet object failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, o_type);
    return (NULL);
  }

  /* Decrease reference counter on the java.lang.String object. */
  (*jvm_env)->DeleteLocalRef (jvm_env, o_type);

  for (i = 0; i < ds->ds_num; i++)
  {
    jobject o_datasource;

    o_datasource = ctoj_data_source (jvm_env, ds->ds + i);
    if (o_datasource == NULL)
    {
      ERROR ("java plugin: ctoj_data_set: ctoj_data_source (%s.%s) failed",
          ds->type, ds->ds[i].name);
      (*jvm_env)->DeleteLocalRef (jvm_env, o_dataset);
      return (NULL);
    }

    (*jvm_env)->CallVoidMethod (jvm_env, o_dataset, m_add, o_datasource);

    (*jvm_env)->DeleteLocalRef (jvm_env, o_datasource);
  } /* for (i = 0; i < ds->ds_num; i++) */

  return (o_dataset);
} /* }}} jobject ctoj_data_set */

static int ctoj_value_list_add_value (JNIEnv *jvm_env, /* {{{ */
    value_t value, int ds_type,
    jclass class_ptr, jobject object_ptr)
{
  jmethodID m_addvalue;
  jobject o_number;

  m_addvalue = (*jvm_env)->GetMethodID (jvm_env, class_ptr,
      "addValue", "(Ljava/lang/Number;)V");
  if (m_addvalue == NULL)
  {
    ERROR ("java plugin: ctoj_value_list_add_value: "
        "Cannot find method `void addValue (Number)'.");
    return (-1);
  }

  o_number = ctoj_value_to_number (jvm_env, value, ds_type);
  if (o_number == NULL)
  {
    ERROR ("java plugin: ctoj_value_list_add_value: "
        "ctoj_value_to_number failed.");
    return (-1);
  }

  (*jvm_env)->CallVoidMethod (jvm_env, object_ptr, m_addvalue, o_number);

  (*jvm_env)->DeleteLocalRef (jvm_env, o_number);

  return (0);
} /* }}} int ctoj_value_list_add_value */

static int ctoj_value_list_add_data_set (JNIEnv *jvm_env, /* {{{ */
    jclass c_valuelist, jobject o_valuelist, const data_set_t *ds)
{
  jmethodID m_setdataset;
  jobject o_dataset;

  /* Look for the `void setDataSource (List<DataSource> ds)' method. */
  m_setdataset = (*jvm_env)->GetMethodID (jvm_env, c_valuelist,
      "setDataSet", "(Lorg/collectd/api/DataSet;)V");
  if (m_setdataset == NULL)
  {
    ERROR ("java plugin: ctoj_value_list_add_data_set: "
        "Cannot find the `void setDataSet (DataSet)' method.");
    return (-1);
  }

  /* Create a DataSet object. */
  o_dataset = ctoj_data_set (jvm_env, ds);
  if (o_dataset == NULL)
  {
    ERROR ("java plugin: ctoj_value_list_add_data_set: "
        "ctoj_data_set (%s) failed.", ds->type);
    return (-1);
  }

  /* Actually call the method. */
  (*jvm_env)->CallVoidMethod (jvm_env,
      o_valuelist, m_setdataset, o_dataset);

  /* Decrease reference counter on the List<DataSource> object. */
  (*jvm_env)->DeleteLocalRef (jvm_env, o_dataset);

  return (0);
} /* }}} int ctoj_value_list_add_data_set */

/* Convert a value_list_t (and data_set_t) to a org/collectd/api/ValueList */
static jobject ctoj_value_list (JNIEnv *jvm_env, /* {{{ */
    const data_set_t *ds, const value_list_t *vl)
{
  jclass c_valuelist;
  jmethodID m_valuelist_constructor;
  jobject o_valuelist;
  int status;
  int i;

  /* First, create a new ValueList instance..
   * Look up the class.. */
  c_valuelist = (*jvm_env)->FindClass (jvm_env,
      "org/collectd/api/ValueList");
  if (c_valuelist == NULL)
  {
    ERROR ("java plugin: ctoj_value_list: "
        "FindClass (org/collectd/api/ValueList) failed.");
    return (NULL);
  }

  /* Lookup the `ValueList ()' constructor. */
  m_valuelist_constructor = (*jvm_env)->GetMethodID (jvm_env, c_valuelist,
      "<init>", "()V");
  if (m_valuelist_constructor == NULL)
  {
    ERROR ("java plugin: ctoj_value_list: Cannot find the "
        "`ValueList ()' constructor.");
    return (NULL);
  }

  /* Create a new instance. */
  o_valuelist = (*jvm_env)->NewObject (jvm_env, c_valuelist,
      m_valuelist_constructor);
  if (o_valuelist == NULL)
  {
    ERROR ("java plugin: ctoj_value_list: Creating a new ValueList instance "
        "failed.");
    return (NULL);
  }

  status = ctoj_value_list_add_data_set (jvm_env,
      c_valuelist, o_valuelist, ds);
  if (status != 0)
  {
    ERROR ("java plugin: ctoj_value_list: "
        "ctoj_value_list_add_data_set failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, o_valuelist);
    return (NULL);
  }

  /* Set the strings.. */
#define SET_STRING(str,method_name) do { \
  status = ctoj_string (jvm_env, str, \
      c_valuelist, o_valuelist, method_name); \
  if (status != 0) { \
    ERROR ("java plugin: ctoj_value_list: ctoj_string (%s) failed.", \
        method_name); \
    (*jvm_env)->DeleteLocalRef (jvm_env, o_valuelist); \
    return (NULL); \
  } } while (0)

  SET_STRING (vl->host,            "setHost");
  SET_STRING (vl->plugin,          "setPlugin");
  SET_STRING (vl->plugin_instance, "setPluginInstance");
  SET_STRING (vl->type,            "setType");
  SET_STRING (vl->type_instance,   "setTypeInstance");

#undef SET_STRING

  /* Set the `time' member. Java stores time in milliseconds. */
  status = ctoj_long (jvm_env, (jlong) CDTIME_T_TO_MS (vl->time),
      c_valuelist, o_valuelist, "setTime");
  if (status != 0)
  {
    ERROR ("java plugin: ctoj_value_list: ctoj_long (setTime) failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, o_valuelist);
    return (NULL);
  }

  /* Set the `interval' member.. */
  status = ctoj_long (jvm_env,
      (jlong) CDTIME_T_TO_MS (vl->interval),
      c_valuelist, o_valuelist, "setInterval");
  if (status != 0)
  {
    ERROR ("java plugin: ctoj_value_list: ctoj_long (setInterval) failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, o_valuelist);
    return (NULL);
  }

  for (i = 0; i < vl->values_len; i++)
  {
    status = ctoj_value_list_add_value (jvm_env, vl->values[i], ds->ds[i].type,
        c_valuelist, o_valuelist);
    if (status != 0)
    {
      ERROR ("java plugin: ctoj_value_list: "
          "ctoj_value_list_add_value failed.");
      (*jvm_env)->DeleteLocalRef (jvm_env, o_valuelist);
      return (NULL);
    }
  }

  return (o_valuelist);
} /* }}} jobject ctoj_value_list */

/* Convert a notification_t to a org/collectd/api/Notification */
static jobject ctoj_notification (JNIEnv *jvm_env, /* {{{ */
    const notification_t *n)
{
  jclass c_notification;
  jmethodID m_constructor;
  jobject o_notification;
  int status;

  /* First, create a new Notification instance..
   * Look up the class.. */
  c_notification = (*jvm_env)->FindClass (jvm_env,
      "org/collectd/api/Notification");
  if (c_notification == NULL)
  {
    ERROR ("java plugin: ctoj_notification: "
        "FindClass (org/collectd/api/Notification) failed.");
    return (NULL);
  }

  /* Lookup the `Notification ()' constructor. */
  m_constructor = (*jvm_env)->GetMethodID (jvm_env, c_notification,
      "<init>", "()V");
  if (m_constructor == NULL)
  {
    ERROR ("java plugin: ctoj_notification: Cannot find the "
        "`Notification ()' constructor.");
    return (NULL);
  }

  /* Create a new instance. */
  o_notification = (*jvm_env)->NewObject (jvm_env, c_notification,
      m_constructor);
  if (o_notification == NULL)
  {
    ERROR ("java plugin: ctoj_notification: Creating a new Notification "
        "instance failed.");
    return (NULL);
  }

  /* Set the strings.. */
#define SET_STRING(str,method_name) do { \
  status = ctoj_string (jvm_env, str, \
      c_notification, o_notification, method_name); \
  if (status != 0) { \
    ERROR ("java plugin: ctoj_notification: ctoj_string (%s) failed.", \
        method_name); \
    (*jvm_env)->DeleteLocalRef (jvm_env, o_notification); \
    return (NULL); \
  } } while (0)

  SET_STRING (n->host,            "setHost");
  SET_STRING (n->plugin,          "setPlugin");
  SET_STRING (n->plugin_instance, "setPluginInstance");
  SET_STRING (n->type,            "setType");
  SET_STRING (n->type_instance,   "setTypeInstance");
  SET_STRING (n->message,         "setMessage");

#undef SET_STRING

  /* Set the `time' member. Java stores time in milliseconds. */
  status = ctoj_long (jvm_env, ((jlong) n->time) * ((jlong) 1000),
      c_notification, o_notification, "setTime");
  if (status != 0)
  {
    ERROR ("java plugin: ctoj_notification: ctoj_long (setTime) failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, o_notification);
    return (NULL);
  }

  /* Set the `severity' member.. */
  status = ctoj_int (jvm_env, (jint) n->severity,
      c_notification, o_notification, "setSeverity");
  if (status != 0)
  {
    ERROR ("java plugin: ctoj_notification: ctoj_int (setSeverity) failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, o_notification);
    return (NULL);
  }

  return (o_notification);
} /* }}} jobject ctoj_notification */

/*
 * Java to C conversion functions
 */
/* Call a `String <method> ()' method. */
static int jtoc_string (JNIEnv *jvm_env, /* {{{ */
    char *buffer, size_t buffer_size, int empty_okay,
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
  if ((string_obj == NULL) && (empty_okay == 0))
  {
    ERROR ("java plugin: jtoc_string: CallObjectMethod (%s) failed.",
        method_name);
    return (-1);
  }
  else if ((string_obj == NULL) && (empty_okay != 0))
  {
    memset (buffer, 0, buffer_size);
    return (0);
  }

  c_str = (*jvm_env)->GetStringUTFChars (jvm_env, string_obj, 0);
  if (c_str == NULL)
  {
    ERROR ("java plugin: jtoc_string: GetStringUTFChars failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, string_obj);
    return (-1);
  }

  sstrncpy (buffer, c_str, buffer_size);

  (*jvm_env)->ReleaseStringUTFChars (jvm_env, string_obj, c_str);
  (*jvm_env)->DeleteLocalRef (jvm_env, string_obj);

  return (0);
} /* }}} int jtoc_string */

/* Call an `int <method> ()' method. */
static int jtoc_int (JNIEnv *jvm_env, /* {{{ */
    jint *ret_value,
    jclass class_ptr, jobject object_ptr, const char *method_name)
{
  jmethodID method_id;

  method_id = (*jvm_env)->GetMethodID (jvm_env, class_ptr,
      method_name, "()I");
  if (method_id == NULL)
  {
    ERROR ("java plugin: jtoc_int: Cannot find method `int %s ()'.",
        method_name);
    return (-1);
  }

  *ret_value = (*jvm_env)->CallIntMethod (jvm_env, object_ptr, method_id);

  return (0);
} /* }}} int jtoc_int */

/* Call a `long <method> ()' method. */
static int jtoc_long (JNIEnv *jvm_env, /* {{{ */
    jlong *ret_value,
    jclass class_ptr, jobject object_ptr, const char *method_name)
{
  jmethodID method_id;

  method_id = (*jvm_env)->GetMethodID (jvm_env, class_ptr,
      method_name, "()J");
  if (method_id == NULL)
  {
    ERROR ("java plugin: jtoc_long: Cannot find method `long %s ()'.",
        method_name);
    return (-1);
  }

  *ret_value = (*jvm_env)->CallLongMethod (jvm_env, object_ptr, method_id);

  return (0);
} /* }}} int jtoc_long */

/* Call a `double <method> ()' method. */
static int jtoc_double (JNIEnv *jvm_env, /* {{{ */
    jdouble *ret_value,
    jclass class_ptr, jobject object_ptr, const char *method_name)
{
  jmethodID method_id;

  method_id = (*jvm_env)->GetMethodID (jvm_env, class_ptr,
      method_name, "()D");
  if (method_id == NULL)
  {
    ERROR ("java plugin: jtoc_double: Cannot find method `double %s ()'.",
        method_name);
    return (-1);
  }

  *ret_value = (*jvm_env)->CallDoubleMethod (jvm_env, object_ptr, method_id);

  return (0);
} /* }}} int jtoc_double */

static int jtoc_value (JNIEnv *jvm_env, /* {{{ */
    value_t *ret_value, int ds_type, jobject object_ptr)
{
  jclass class_ptr;
  int status;

  class_ptr = (*jvm_env)->GetObjectClass (jvm_env, object_ptr);

  if (ds_type == DS_TYPE_GAUGE)
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
  else
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

    if (ds_type == DS_TYPE_DERIVE)
      (*ret_value).derive = (derive_t) tmp_long;
    else if (ds_type == DS_TYPE_ABSOLUTE)
      (*ret_value).absolute = (absolute_t) tmp_long;
    else
      (*ret_value).counter = (counter_t) tmp_long;
  }

  return (0);
} /* }}} int jtoc_value */

/* Read a List<Number>, convert it to `value_t' and add it to the given
 * `value_list_t'. */
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

  values = (value_t *) calloc (values_num, sizeof (value_t));
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

/* Convert a org/collectd/api/ValueList to a value_list_t. */
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

  /* eo == empty okay */
#define SET_STRING(buffer,method, eo) do { \
  status = jtoc_string (jvm_env, buffer, sizeof (buffer), eo, \
      class_ptr, object_ptr, method); \
  if (status != 0) { \
    ERROR ("java plugin: jtoc_value_list: jtoc_string (%s) failed.", \
        method); \
    return (-1); \
  } } while (0)

  SET_STRING(vl->type, "getType", /* empty = */ 0);

  ds = plugin_get_ds (vl->type);
  if (ds == NULL)
  {
    ERROR ("java plugin: jtoc_value_list: Data-set `%s' is not defined. "
        "Please consult the types.db(5) manpage for mor information.",
        vl->type);
    return (-1);
  }

  SET_STRING(vl->host,            "getHost",           /* empty = */ 0);
  SET_STRING(vl->plugin,          "getPlugin",         /* empty = */ 0);
  SET_STRING(vl->plugin_instance, "getPluginInstance", /* empty = */ 1);
  SET_STRING(vl->type_instance,   "getTypeInstance",   /* empty = */ 1);

#undef SET_STRING

  status = jtoc_long (jvm_env, &tmp_long, class_ptr, object_ptr, "getTime");
  if (status != 0)
  {
    ERROR ("java plugin: jtoc_value_list: jtoc_long (getTime) failed.");
    return (-1);
  }
  /* Java measures time in milliseconds. */
  vl->time = MS_TO_CDTIME_T (tmp_long);

  status = jtoc_long (jvm_env, &tmp_long,
      class_ptr, object_ptr, "getInterval");
  if (status != 0)
  {
    ERROR ("java plugin: jtoc_value_list: jtoc_long (getInterval) failed.");
    return (-1);
  }
  vl->interval = MS_TO_CDTIME_T (tmp_long);

  status = jtoc_values_array (jvm_env, ds, vl, class_ptr, object_ptr);
  if (status != 0)
  {
    ERROR ("java plugin: jtoc_value_list: jtoc_values_array failed.");
    return (-1);
  }

  return (0);
} /* }}} int jtoc_value_list */

/* Convert a org/collectd/api/Notification to a notification_t. */
static int jtoc_notification (JNIEnv *jvm_env, notification_t *n, /* {{{ */
    jobject object_ptr)
{
  jclass class_ptr;
  int status;
  jlong tmp_long;
  jint tmp_int;

  class_ptr = (*jvm_env)->GetObjectClass (jvm_env, object_ptr);
  if (class_ptr == NULL)
  {
    ERROR ("java plugin: jtoc_notification: GetObjectClass failed.");
    return (-1);
  }

  /* eo == empty okay */
#define SET_STRING(buffer,method, eo) do { \
  status = jtoc_string (jvm_env, buffer, sizeof (buffer), eo, \
      class_ptr, object_ptr, method); \
  if (status != 0) { \
    ERROR ("java plugin: jtoc_notification: jtoc_string (%s) failed.", \
        method); \
    return (-1); \
  } } while (0)

  SET_STRING (n->host,            "getHost",           /* empty = */ 1);
  SET_STRING (n->plugin,          "getPlugin",         /* empty = */ 1);
  SET_STRING (n->plugin_instance, "getPluginInstance", /* empty = */ 1);
  SET_STRING (n->type,            "getType",           /* empty = */ 1);
  SET_STRING (n->type_instance,   "getTypeInstance",   /* empty = */ 1);
  SET_STRING (n->message,         "getMessage",        /* empty = */ 0);

#undef SET_STRING

  status = jtoc_long (jvm_env, &tmp_long, class_ptr, object_ptr, "getTime");
  if (status != 0)
  {
    ERROR ("java plugin: jtoc_notification: jtoc_long (getTime) failed.");
    return (-1);
  }
  /* Java measures time in milliseconds. */
  n->time = (time_t) (tmp_long / ((jlong) 1000));

  status = jtoc_int (jvm_env, &tmp_int,
      class_ptr, object_ptr, "getSeverity");
  if (status != 0)
  {
    ERROR ("java plugin: jtoc_notification: jtoc_int (getSeverity) failed.");
    return (-1);
  }
  n->severity = (int) tmp_int;

  return (0);
} /* }}} int jtoc_notification */
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

  status = plugin_dispatch_values (&vl);

  sfree (vl.values);

  return (status);
} /* }}} jint cjni_api_dispatch_values */

static jint JNICALL cjni_api_dispatch_notification (JNIEnv *jvm_env, /* {{{ */
    jobject this, jobject o_notification)
{
  notification_t n;
  int status;

  memset (&n, 0, sizeof (n));
  n.meta = NULL;

  status = jtoc_notification (jvm_env, &n, o_notification);
  if (status != 0)
  {
    ERROR ("java plugin: cjni_api_dispatch_notification: jtoc_notification failed.");
    return (-1);
  }

  status = plugin_dispatch_notification (&n);

  return (status);
} /* }}} jint cjni_api_dispatch_notification */

static jobject JNICALL cjni_api_get_ds (JNIEnv *jvm_env, /* {{{ */
    jobject this, jobject o_string_type)
{
  const char *ds_name;
  const data_set_t *ds;
  jobject o_dataset;

  ds_name = (*jvm_env)->GetStringUTFChars (jvm_env, o_string_type, 0);
  if (ds_name == NULL)
  {
    ERROR ("java plugin: cjni_api_get_ds: GetStringUTFChars failed.");
    return (NULL);
  }

  ds = plugin_get_ds (ds_name);
  DEBUG ("java plugin: cjni_api_get_ds: "
      "plugin_get_ds (%s) = %p;", ds_name, (void *) ds);

  (*jvm_env)->ReleaseStringUTFChars (jvm_env, o_string_type, ds_name);

  if (ds == NULL)
    return (NULL);

  o_dataset = ctoj_data_set (jvm_env, ds);
  return (o_dataset);
} /* }}} jint cjni_api_get_ds */

static jint JNICALL cjni_api_register_config (JNIEnv *jvm_env, /* {{{ */
    jobject this, jobject o_name, jobject o_config)
{
  return (cjni_callback_register (jvm_env, o_name, o_config, CB_TYPE_CONFIG));
} /* }}} jint cjni_api_register_config */

static jint JNICALL cjni_api_register_init (JNIEnv *jvm_env, /* {{{ */
    jobject this, jobject o_name, jobject o_config)
{
  return (cjni_callback_register (jvm_env, o_name, o_config, CB_TYPE_INIT));
} /* }}} jint cjni_api_register_init */

static jint JNICALL cjni_api_register_read (JNIEnv *jvm_env, /* {{{ */
    jobject this, jobject o_name, jobject o_read)
{
  user_data_t ud;
  cjni_callback_info_t *cbi;

  cbi = cjni_callback_info_create (jvm_env, o_name, o_read, CB_TYPE_READ);
  if (cbi == NULL)
    return (-1);

  DEBUG ("java plugin: Registering new read callback: %s", cbi->name);

  memset (&ud, 0, sizeof (ud));
  ud.data = (void *) cbi;
  ud.free_func = cjni_callback_info_destroy;

  plugin_register_complex_read (/* group = */ NULL, cbi->name, cjni_read,
      /* interval = */ NULL, &ud);

  (*jvm_env)->DeleteLocalRef (jvm_env, o_read);

  return (0);
} /* }}} jint cjni_api_register_read */

static jint JNICALL cjni_api_register_write (JNIEnv *jvm_env, /* {{{ */
    jobject this, jobject o_name, jobject o_write)
{
  user_data_t ud;
  cjni_callback_info_t *cbi;

  cbi = cjni_callback_info_create (jvm_env, o_name, o_write, CB_TYPE_WRITE);
  if (cbi == NULL)
    return (-1);

  DEBUG ("java plugin: Registering new write callback: %s", cbi->name);

  memset (&ud, 0, sizeof (ud));
  ud.data = (void *) cbi;
  ud.free_func = cjni_callback_info_destroy;

  plugin_register_write (cbi->name, cjni_write, &ud);

  (*jvm_env)->DeleteLocalRef (jvm_env, o_write);

  return (0);
} /* }}} jint cjni_api_register_write */

static jint JNICALL cjni_api_register_flush (JNIEnv *jvm_env, /* {{{ */
    jobject this, jobject o_name, jobject o_flush)
{
  user_data_t ud;
  cjni_callback_info_t *cbi;

  cbi = cjni_callback_info_create (jvm_env, o_name, o_flush, CB_TYPE_FLUSH);
  if (cbi == NULL)
    return (-1);

  DEBUG ("java plugin: Registering new flush callback: %s", cbi->name);

  memset (&ud, 0, sizeof (ud));
  ud.data = (void *) cbi;
  ud.free_func = cjni_callback_info_destroy;

  plugin_register_flush (cbi->name, cjni_flush, &ud);

  (*jvm_env)->DeleteLocalRef (jvm_env, o_flush);

  return (0);
} /* }}} jint cjni_api_register_flush */

static jint JNICALL cjni_api_register_shutdown (JNIEnv *jvm_env, /* {{{ */
    jobject this, jobject o_name, jobject o_shutdown)
{
  return (cjni_callback_register (jvm_env, o_name, o_shutdown,
        CB_TYPE_SHUTDOWN));
} /* }}} jint cjni_api_register_shutdown */

static jint JNICALL cjni_api_register_log (JNIEnv *jvm_env, /* {{{ */
    jobject this, jobject o_name, jobject o_log)
{
  user_data_t ud;
  cjni_callback_info_t *cbi;

  cbi = cjni_callback_info_create (jvm_env, o_name, o_log, CB_TYPE_LOG);
  if (cbi == NULL)
    return (-1);

  DEBUG ("java plugin: Registering new log callback: %s", cbi->name);

  memset (&ud, 0, sizeof (ud));
  ud.data = (void *) cbi;
  ud.free_func = cjni_callback_info_destroy;

  plugin_register_log (cbi->name, cjni_log, &ud);

  (*jvm_env)->DeleteLocalRef (jvm_env, o_log);

  return (0);
} /* }}} jint cjni_api_register_log */

static jint JNICALL cjni_api_register_notification (JNIEnv *jvm_env, /* {{{ */
    jobject this, jobject o_name, jobject o_notification)
{
  user_data_t ud;
  cjni_callback_info_t *cbi;

  cbi = cjni_callback_info_create (jvm_env, o_name, o_notification,
      CB_TYPE_NOTIFICATION);
  if (cbi == NULL)
    return (-1);

  DEBUG ("java plugin: Registering new notification callback: %s", cbi->name);

  memset (&ud, 0, sizeof (ud));
  ud.data = (void *) cbi;
  ud.free_func = cjni_callback_info_destroy;

  plugin_register_notification (cbi->name, cjni_notification, &ud);

  (*jvm_env)->DeleteLocalRef (jvm_env, o_notification);

  return (0);
} /* }}} jint cjni_api_register_notification */

static jint JNICALL cjni_api_register_match_target (JNIEnv *jvm_env, /* {{{ */
    jobject this, jobject o_name, jobject o_match, int type)
{
  int status;
  const char *c_name;

  c_name = (*jvm_env)->GetStringUTFChars (jvm_env, o_name, 0);
  if (c_name == NULL)
  {
    ERROR ("java plugin: cjni_api_register_match_target: "
        "GetStringUTFChars failed.");
    return (-1);
  }

  status = cjni_callback_register (jvm_env, o_name, o_match, type);
  if (status != 0)
  {
    (*jvm_env)->ReleaseStringUTFChars (jvm_env, o_name, c_name);
    return (-1);
  }

  if (type == CB_TYPE_MATCH)
  {
    match_proc_t m_proc;

    memset (&m_proc, 0, sizeof (m_proc));
    m_proc.create  = cjni_match_target_create;
    m_proc.destroy = cjni_match_target_destroy;
    m_proc.match   = (void *) cjni_match_target_invoke;

    status = fc_register_match (c_name, m_proc);
  }
  else if (type == CB_TYPE_TARGET)
  {
    target_proc_t t_proc;

    memset (&t_proc, 0, sizeof (t_proc));
    t_proc.create  = cjni_match_target_create;
    t_proc.destroy = cjni_match_target_destroy;
    t_proc.invoke  = cjni_match_target_invoke;

    status = fc_register_target (c_name, t_proc);
  }
  else
  {
    ERROR ("java plugin: cjni_api_register_match_target: "
        "Don't know whether to create a match or a target.");
    (*jvm_env)->ReleaseStringUTFChars (jvm_env, o_name, c_name);
    return (-1);
  }

  if (status != 0)
  {
    ERROR ("java plugin: cjni_api_register_match_target: "
        "%s failed.",
        (type == CB_TYPE_MATCH) ? "fc_register_match" : "fc_register_target");
    (*jvm_env)->ReleaseStringUTFChars (jvm_env, o_name, c_name);
    return (-1);
  }

  (*jvm_env)->ReleaseStringUTFChars (jvm_env, o_name, c_name);

  return (0);
} /* }}} jint cjni_api_register_match_target */

static jint JNICALL cjni_api_register_match (JNIEnv *jvm_env, /* {{{ */
    jobject this, jobject o_name, jobject o_match)
{
  return (cjni_api_register_match_target (jvm_env, this, o_name, o_match,
        CB_TYPE_MATCH));
} /* }}} jint cjni_api_register_match */

static jint JNICALL cjni_api_register_target (JNIEnv *jvm_env, /* {{{ */
    jobject this, jobject o_name, jobject o_target)
{
  return (cjni_api_register_match_target (jvm_env, this, o_name, o_target,
        CB_TYPE_TARGET));
} /* }}} jint cjni_api_register_target */

static void JNICALL cjni_api_log (JNIEnv *jvm_env, /* {{{ */
    jobject this, jint severity, jobject o_message)
{
  const char *c_str;

  c_str = (*jvm_env)->GetStringUTFChars (jvm_env, o_message, 0);
  if (c_str == NULL)
  {
    ERROR ("java plugin: cjni_api_log: GetStringUTFChars failed.");
    return;
  }

  if (severity < LOG_ERR)
    severity = LOG_ERR;
  if (severity > LOG_DEBUG)
    severity = LOG_DEBUG;

  plugin_log (severity, "%s", c_str);

  (*jvm_env)->ReleaseStringUTFChars (jvm_env, o_message, c_str);
} /* }}} void cjni_api_log */

/* List of ``native'' functions, i. e. C-functions that can be called from
 * Java. */
static JNINativeMethod jni_api_functions[] = /* {{{ */
{
  { "dispatchValues",
    "(Lorg/collectd/api/ValueList;)I",
    cjni_api_dispatch_values },

  { "dispatchNotification",
    "(Lorg/collectd/api/Notification;)I",
    cjni_api_dispatch_notification },

  { "getDS",
    "(Ljava/lang/String;)Lorg/collectd/api/DataSet;",
    cjni_api_get_ds },

  { "registerConfig",
    "(Ljava/lang/String;Lorg/collectd/api/CollectdConfigInterface;)I",
    cjni_api_register_config },

  { "registerInit",
    "(Ljava/lang/String;Lorg/collectd/api/CollectdInitInterface;)I",
    cjni_api_register_init },

  { "registerRead",
    "(Ljava/lang/String;Lorg/collectd/api/CollectdReadInterface;)I",
    cjni_api_register_read },

  { "registerWrite",
    "(Ljava/lang/String;Lorg/collectd/api/CollectdWriteInterface;)I",
    cjni_api_register_write },

  { "registerFlush",
    "(Ljava/lang/String;Lorg/collectd/api/CollectdFlushInterface;)I",
    cjni_api_register_flush },

  { "registerShutdown",
    "(Ljava/lang/String;Lorg/collectd/api/CollectdShutdownInterface;)I",
    cjni_api_register_shutdown },

  { "registerLog",
    "(Ljava/lang/String;Lorg/collectd/api/CollectdLogInterface;)I",
    cjni_api_register_log },

  { "registerNotification",
    "(Ljava/lang/String;Lorg/collectd/api/CollectdNotificationInterface;)I",
    cjni_api_register_notification },

  { "registerMatch",
    "(Ljava/lang/String;Lorg/collectd/api/CollectdMatchFactoryInterface;)I",
    cjni_api_register_match },

  { "registerTarget",
    "(Ljava/lang/String;Lorg/collectd/api/CollectdTargetFactoryInterface;)I",
    cjni_api_register_target },

  { "log",
    "(ILjava/lang/String;)V",
    cjni_api_log },
};
static size_t jni_api_functions_num = sizeof (jni_api_functions)
  / sizeof (jni_api_functions[0]);
/* }}} */

/*
 * Functions
 */
/* Allocate a `cjni_callback_info_t' given the type and objects necessary for
 * all registration functions. */
static cjni_callback_info_t *cjni_callback_info_create (JNIEnv *jvm_env, /* {{{ */
    jobject o_name, jobject o_callback, int type)
{
  const char *c_name;
  cjni_callback_info_t *cbi;
  const char *method_name;
  const char *method_signature;

  switch (type)
  {
    case CB_TYPE_CONFIG:
      method_name = "config";
      method_signature = "(Lorg/collectd/api/OConfigItem;)I";
      break;

    case CB_TYPE_INIT:
      method_name = "init";
      method_signature = "()I";
      break;

    case CB_TYPE_READ:
      method_name = "read";
      method_signature = "()I";
      break;

    case CB_TYPE_WRITE:
      method_name = "write";
      method_signature = "(Lorg/collectd/api/ValueList;)I";
      break;

    case CB_TYPE_FLUSH:
      method_name = "flush";
      method_signature = "(Ljava/lang/Number;Ljava/lang/String;)I";
      break;

    case CB_TYPE_SHUTDOWN:
      method_name = "shutdown";
      method_signature = "()I";
      break;

    case CB_TYPE_LOG:
      method_name = "log";
      method_signature = "(ILjava/lang/String;)V";
      break;

    case CB_TYPE_NOTIFICATION:
      method_name = "notification";
      method_signature = "(Lorg/collectd/api/Notification;)I";
      break;

    case CB_TYPE_MATCH:
      method_name = "createMatch";
      method_signature = "(Lorg/collectd/api/OConfigItem;)"
        "Lorg/collectd/api/CollectdMatchInterface;";
      break;

    case CB_TYPE_TARGET:
      method_name = "createTarget";
      method_signature = "(Lorg/collectd/api/OConfigItem;)"
        "Lorg/collectd/api/CollectdTargetInterface;";
      break;

    default:
      ERROR ("java plugin: cjni_callback_info_create: Unknown type: %#x",
          type);
      return (NULL);
  }

  c_name = (*jvm_env)->GetStringUTFChars (jvm_env, o_name, 0);
  if (c_name == NULL)
  {
    ERROR ("java plugin: cjni_callback_info_create: "
        "GetStringUTFChars failed.");
    return (NULL);
  }

  cbi = (cjni_callback_info_t *) malloc (sizeof (*cbi));
  if (cbi == NULL)
  {
    ERROR ("java plugin: cjni_callback_info_create: malloc failed.");
    (*jvm_env)->ReleaseStringUTFChars (jvm_env, o_name, c_name);
    return (NULL);
  }
  memset (cbi, 0, sizeof (*cbi));
  cbi->type = type;

  cbi->name = strdup (c_name);
  if (cbi->name == NULL)
  {
    pthread_mutex_unlock (&java_callbacks_lock);
    ERROR ("java plugin: cjni_callback_info_create: strdup failed.");
    (*jvm_env)->ReleaseStringUTFChars (jvm_env, o_name, c_name);
    return (NULL);
  }

  (*jvm_env)->ReleaseStringUTFChars (jvm_env, o_name, c_name);

  cbi->object = (*jvm_env)->NewGlobalRef (jvm_env, o_callback);
  if (cbi->object == NULL)
  {
    ERROR ("java plugin: cjni_callback_info_create: NewGlobalRef failed.");
    free (cbi);
    return (NULL);
  }

  cbi->class  = (*jvm_env)->GetObjectClass (jvm_env, cbi->object);
  if (cbi->class == NULL)
  {
    ERROR ("java plugin: cjni_callback_info_create: GetObjectClass failed.");
    free (cbi);
    return (NULL);
  }

  cbi->method = (*jvm_env)->GetMethodID (jvm_env, cbi->class,
      method_name, method_signature);
  if (cbi->method == NULL)
  {
    ERROR ("java plugin: cjni_callback_info_create: "
        "Cannot find the `%s' method with signature `%s'.",
        method_name, method_signature);
    free (cbi);
    return (NULL);
  }

  return (cbi);
} /* }}} cjni_callback_info_t cjni_callback_info_create */

/* Allocate a `cjni_callback_info_t' via `cjni_callback_info_create' and add it
 * to the global `java_callbacks' variable. This is used for `config', `init',
 * and `shutdown' callbacks. */
static int cjni_callback_register (JNIEnv *jvm_env, /* {{{ */
    jobject o_name, jobject o_callback, int type)
{
  cjni_callback_info_t *cbi;
  cjni_callback_info_t *tmp;
#if COLLECT_DEBUG
  const char *type_str;
#endif

  cbi = cjni_callback_info_create (jvm_env, o_name, o_callback, type);
  if (cbi == NULL)
    return (-1);

#if COLLECT_DEBUG
  switch (type)
  {
    case CB_TYPE_CONFIG:
      type_str = "config";
      break;

    case CB_TYPE_INIT:
      type_str = "init";
      break;

    case CB_TYPE_SHUTDOWN:
      type_str = "shutdown";
      break;

    case CB_TYPE_MATCH:
      type_str = "match";
      break;

    case CB_TYPE_TARGET:
      type_str = "target";
      break;

    default:
      type_str = "<unknown>";
  }
  DEBUG ("java plugin: Registering new %s callback: %s",
      type_str, cbi->name);
#endif

  pthread_mutex_lock (&java_callbacks_lock);

  tmp = (cjni_callback_info_t *) realloc (java_callbacks,
      (java_callbacks_num + 1) * sizeof (*java_callbacks));
  if (tmp == NULL)
  {
    pthread_mutex_unlock (&java_callbacks_lock);
    ERROR ("java plugin: cjni_callback_register: realloc failed.");

    (*jvm_env)->DeleteGlobalRef (jvm_env, cbi->object);
    free (cbi);

    return (-1);
  }
  java_callbacks = tmp;
  java_callbacks[java_callbacks_num] = *cbi;
  java_callbacks_num++;

  pthread_mutex_unlock (&java_callbacks_lock);

  free (cbi);
  return (0);
} /* }}} int cjni_callback_register */

/* Callback for `pthread_key_create'. It frees the data contained in
 * `jvm_env_key' and prints a warning if the reference counter is not zero. */
static void cjni_jvm_env_destroy (void *args) /* {{{ */
{
  cjni_jvm_env_t *cjni_env;

  if (args == NULL)
    return;

  cjni_env = (cjni_jvm_env_t *) args;

  if (cjni_env->reference_counter > 0)
  {
    ERROR ("java plugin: cjni_jvm_env_destroy: "
        "cjni_env->reference_counter = %i;", cjni_env->reference_counter);
  }

  if (cjni_env->jvm_env != NULL)
  {
    ERROR ("java plugin: cjni_jvm_env_destroy: cjni_env->jvm_env = %p;",
        (void *) cjni_env->jvm_env);
  }

  /* The pointer is allocated in `cjni_thread_attach' */
  free (cjni_env);
} /* }}} void cjni_jvm_env_destroy */

/* Register ``native'' functions with the JVM. Native functions are C-functions
 * that can be called by Java code. */
static int cjni_init_native (JNIEnv *jvm_env) /* {{{ */
{
  jclass api_class_ptr;
  int status;

  api_class_ptr = (*jvm_env)->FindClass (jvm_env, "org/collectd/api/Collectd");
  if (api_class_ptr == NULL)
  {
    ERROR ("cjni_init_native: Cannot find the API class \"org.collectd.api"
        ".Collectd\". Please set the correct class path "
        "using 'JVMArg \"-Djava.class.path=...\"'.");
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

/* Create the JVM. This is called when the first thread tries to access the JVM
 * via cjni_thread_attach. */
static int cjni_create_jvm (void) /* {{{ */
{
  JNIEnv *jvm_env;
  JavaVMInitArgs vm_args;
  JavaVMOption vm_options[jvm_argc];

  int status;
  size_t i;

  if (jvm != NULL)
    return (0);

  status = pthread_key_create (&jvm_env_key, cjni_jvm_env_destroy);
  if (status != 0)
  {
    ERROR ("java plugin: cjni_create_jvm: pthread_key_create failed "
        "with status %i.", status);
    return (-1);
  }

  jvm_env = NULL;

  memset (&vm_args, 0, sizeof (vm_args));
  vm_args.version = JNI_VERSION_1_2;
  vm_args.options = vm_options;
  vm_args.nOptions = (jint) jvm_argc;

  for (i = 0; i < jvm_argc; i++)
  {
    DEBUG ("java plugin: cjni_create_jvm: jvm_argv[%zu] = %s",
        i, jvm_argv[i]);
    vm_args.options[i].optionString = jvm_argv[i];
  }

  status = JNI_CreateJavaVM (&jvm, (void *) &jvm_env, (void *) &vm_args);
  if (status != 0)
  {
    ERROR ("java plugin: cjni_create_jvm: "
        "JNI_CreateJavaVM failed with status %i.",
	status);
    return (-1);
  }
  assert (jvm != NULL);
  assert (jvm_env != NULL);

  /* Call RegisterNatives */
  status = cjni_init_native (jvm_env);
  if (status != 0)
  {
    ERROR ("java plugin: cjni_create_jvm: cjni_init_native failed.");
    return (-1);
  }

  DEBUG ("java plugin: The JVM has been created.");
  return (0);
} /* }}} int cjni_create_jvm */

/* Increase the reference counter to the JVM for this thread. If it was zero,
 * attach the JVM first. */
static JNIEnv *cjni_thread_attach (void) /* {{{ */
{
  cjni_jvm_env_t *cjni_env;
  JNIEnv *jvm_env;

  /* If we're the first thread to access the JVM, we'll have to create it
   * first.. */
  if (jvm == NULL)
  {
    int status;

    status = cjni_create_jvm ();
    if (status != 0)
    {
      ERROR ("java plugin: cjni_thread_attach: cjni_create_jvm failed.");
      return (NULL);
    }
  }
  assert (jvm != NULL);

  cjni_env = pthread_getspecific (jvm_env_key);
  if (cjni_env == NULL)
  {
    /* This pointer is free'd in `cjni_jvm_env_destroy'. */
    cjni_env = (cjni_jvm_env_t *) malloc (sizeof (*cjni_env));
    if (cjni_env == NULL)
    {
      ERROR ("java plugin: cjni_thread_attach: malloc failed.");
      return (NULL);
    }
    memset (cjni_env, 0, sizeof (*cjni_env));
    cjni_env->reference_counter = 0;
    cjni_env->jvm_env = NULL;

    pthread_setspecific (jvm_env_key, cjni_env);
  }

  if (cjni_env->reference_counter > 0)
  {
    cjni_env->reference_counter++;
    jvm_env = cjni_env->jvm_env;
  }
  else
  {
    int status;
    JavaVMAttachArgs args;

    assert (cjni_env->jvm_env == NULL);

    memset (&args, 0, sizeof (args));
    args.version = JNI_VERSION_1_2;

    status = (*jvm)->AttachCurrentThread (jvm, (void *) &jvm_env, (void *) &args);
    if (status != 0)
    {
      ERROR ("java plugin: cjni_thread_attach: AttachCurrentThread failed "
          "with status %i.", status);
      return (NULL);
    }

    cjni_env->reference_counter = 1;
    cjni_env->jvm_env = jvm_env;
  }

  DEBUG ("java plugin: cjni_thread_attach: cjni_env->reference_counter = %i",
      cjni_env->reference_counter);
  assert (jvm_env != NULL);
  return (jvm_env);
} /* }}} JNIEnv *cjni_thread_attach */

/* Decrease the reference counter of this thread. If it reaches zero, detach
 * from the JVM. */
static int cjni_thread_detach (void) /* {{{ */
{
  cjni_jvm_env_t *cjni_env;
  int status;

  cjni_env = pthread_getspecific (jvm_env_key);
  if (cjni_env == NULL)
  {
    ERROR ("java plugin: cjni_thread_detach: pthread_getspecific failed.");
    return (-1);
  }

  assert (cjni_env->reference_counter > 0);
  assert (cjni_env->jvm_env != NULL);

  cjni_env->reference_counter--;
  DEBUG ("java plugin: cjni_thread_detach: cjni_env->reference_counter = %i",
      cjni_env->reference_counter);

  if (cjni_env->reference_counter > 0)
    return (0);

  status = (*jvm)->DetachCurrentThread (jvm);
  if (status != 0)
  {
    ERROR ("java plugin: cjni_thread_detach: DetachCurrentThread failed "
        "with status %i.", status);
  }

  cjni_env->reference_counter = 0;
  cjni_env->jvm_env = NULL;

  return (0);
} /* }}} JNIEnv *cjni_thread_attach */

static int cjni_config_add_jvm_arg (oconfig_item_t *ci) /* {{{ */
{
  char **tmp;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("java plugin: `JVMArg' needs exactly one string argument.");
    return (-1);
  }

  if (jvm != NULL)
  {
    ERROR ("java plugin: All `JVMArg' options MUST appear before all "
        "`LoadPlugin' options! The JVM is already started and I have to "
        "ignore this argument: %s",
        ci->values[0].value.string);
    return (-1);
  }

  tmp = (char **) realloc (jvm_argv, sizeof (char *) * (jvm_argc + 1));
  if (tmp == NULL)
  {
    ERROR ("java plugin: realloc failed.");
    return (-1);
  }
  jvm_argv = tmp;

  jvm_argv[jvm_argc] = strdup (ci->values[0].value.string);
  if (jvm_argv[jvm_argc] == NULL)
  {
    ERROR ("java plugin: strdup failed.");
    return (-1);
  }
  jvm_argc++;

  return (0);
} /* }}} int cjni_config_add_jvm_arg */

static int cjni_config_load_plugin (oconfig_item_t *ci) /* {{{ */
{
  JNIEnv *jvm_env;
  java_plugin_class_t *class;
  jmethodID constructor_id;
  jobject tmp_object;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("java plugin: `LoadPlugin' needs exactly one string argument.");
    return (-1);
  }

  jvm_env = cjni_thread_attach ();
  if (jvm_env == NULL)
    return (-1);

  class = (java_plugin_class_t *) realloc (java_classes_list,
      (java_classes_list_len + 1) * sizeof (*java_classes_list));
  if (class == NULL)
  {
    ERROR ("java plugin: realloc failed.");
    cjni_thread_detach ();
    return (-1);
  }
  java_classes_list = class;
  class = java_classes_list + java_classes_list_len;

  memset (class, 0, sizeof (*class));
  class->name = strdup (ci->values[0].value.string);
  if (class->name == NULL)
  {
    ERROR ("java plugin: strdup failed.");
    cjni_thread_detach ();
    return (-1);
  }
  class->class = NULL;
  class->object = NULL;

  { /* Replace all dots ('.') with slashes ('/'). Dots are usually used
       thorough the Java community, but (Sun's) `FindClass' and friends need
       slashes. */
    size_t i;
    for (i = 0; class->name[i] != 0; i++)
      if (class->name[i] == '.')
        class->name[i] = '/';
  }

  DEBUG ("java plugin: Loading class %s", class->name);

  class->class = (*jvm_env)->FindClass (jvm_env, class->name);
  if (class->class == NULL)
  {
    ERROR ("java plugin: cjni_config_load_plugin: FindClass (%s) failed.",
        class->name);
    cjni_thread_detach ();
    free (class->name);
    return (-1);
  }

  constructor_id = (*jvm_env)->GetMethodID (jvm_env, class->class,
      "<init>", "()V");
  if (constructor_id == NULL)
  {
    ERROR ("java plugin: cjni_config_load_plugin: "
        "Could not find the constructor for `%s'.",
        class->name);
    cjni_thread_detach ();
    free (class->name);
    return (-1);
  }

  tmp_object = (*jvm_env)->NewObject (jvm_env, class->class,
      constructor_id);
  if (tmp_object != NULL)
    class->object = (*jvm_env)->NewGlobalRef (jvm_env, tmp_object);
  else
    class->object = NULL;
  if (class->object == NULL)
  {
    ERROR ("java plugin: cjni_config_load_plugin: "
        "Could create a new `%s' object.",
        class->name);
    cjni_thread_detach ();
    free (class->name);
    return (-1);
  }

  cjni_thread_detach ();

  java_classes_list_len++;

  return (0);
} /* }}} int cjni_config_load_plugin */

static int cjni_config_plugin_block (oconfig_item_t *ci) /* {{{ */
{
  JNIEnv *jvm_env;
  cjni_callback_info_t *cbi;
  jobject o_ocitem;
  const char *name;
  size_t i;

  jclass class;
  jmethodID method;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("java plugin: `Plugin' blocks "
        "need exactly one string argument.");
    return (-1);
  }

  name = ci->values[0].value.string;

  cbi = NULL;
  for (i = 0; i < java_callbacks_num; i++)
  {
    if (java_callbacks[i].type != CB_TYPE_CONFIG)
      continue;

    if (strcmp (name, java_callbacks[i].name) != 0)
      continue;

    cbi = java_callbacks + i;
    break;
  }

  if (cbi == NULL)
  {
    NOTICE ("java plugin: Configuration block for `%s' found, but no such "
        "configuration callback has been registered. Please make sure, the "
        "`LoadPlugin' lines precede the `Plugin' blocks.",
        name);
    return (0);
  }

  DEBUG ("java plugin: Configuring %s", name);

  jvm_env = cjni_thread_attach ();
  if (jvm_env == NULL)
    return (-1);

  o_ocitem = ctoj_oconfig_item (jvm_env, ci);
  if (o_ocitem == NULL)
  {
    ERROR ("java plugin: cjni_config_plugin_block: ctoj_oconfig_item failed.");
    cjni_thread_detach ();
    return (-1);
  }

  class = (*jvm_env)->GetObjectClass (jvm_env, cbi->object);
  method = (*jvm_env)->GetMethodID (jvm_env, class,
      "config", "(Lorg/collectd/api/OConfigItem;)I");

  (*jvm_env)->CallIntMethod (jvm_env,
      cbi->object, method, o_ocitem);

  (*jvm_env)->DeleteLocalRef (jvm_env, o_ocitem);
  cjni_thread_detach ();
  return (0);
} /* }}} int cjni_config_plugin_block */

static int cjni_config_perform (oconfig_item_t *ci) /* {{{ */
{
  int success;
  int errors;
  int status;
  int i;

  success = 0;
  errors = 0;

  for (i = 0; i < ci->children_num; i++)
  {
    oconfig_item_t *child = ci->children + i;

    if (strcasecmp ("JVMArg", child->key) == 0)
    {
      status = cjni_config_add_jvm_arg (child);
      if (status == 0)
        success++;
      else
        errors++;
    }
    else if (strcasecmp ("LoadPlugin", child->key) == 0)
    {
      status = cjni_config_load_plugin (child);
      if (status == 0)
        success++;
      else
        errors++;
    }
    else if (strcasecmp ("Plugin", child->key) == 0)
    {
      status = cjni_config_plugin_block (child);
      if (status == 0)
        success++;
      else
        errors++;
    }
    else
    {
      WARNING ("java plugin: Option `%s' not allowed here.", child->key);
      errors++;
    }
  }

  DEBUG ("java plugin: jvm_argc = %zu;", jvm_argc);
  DEBUG ("java plugin: java_classes_list_len = %zu;", java_classes_list_len);

  if ((success == 0) && (errors > 0))
  {
    ERROR ("java plugin: All statements failed.");
    return (-1);
  }

  return (0);
} /* }}} int cjni_config_perform */

/* Copy the children of `ci' to the global `config_block' variable. */
static int cjni_config_callback (oconfig_item_t *ci) /* {{{ */
{
  oconfig_item_t *ci_copy;
  oconfig_item_t *tmp;

  assert (ci != NULL);
  if (ci->children_num == 0)
    return (0); /* nothing to do */

  ci_copy = oconfig_clone (ci);
  if (ci_copy == NULL)
  {
    ERROR ("java plugin: oconfig_clone failed.");
    return (-1);
  }

  if (config_block == NULL)
  {
    config_block = ci_copy;
    return (0);
  }

  tmp = realloc (config_block->children,
      (config_block->children_num + ci_copy->children_num) * sizeof (*tmp));
  if (tmp == NULL)
  {
    ERROR ("java plugin: realloc failed.");
    oconfig_free (ci_copy);
    return (-1);
  }
  config_block->children = tmp;

  /* Copy the pointers */
  memcpy (config_block->children + config_block->children_num,
      ci_copy->children,
      ci_copy->children_num * sizeof (*ci_copy->children));
  config_block->children_num += ci_copy->children_num;

  /* Delete the pointers from the copy, so `oconfig_free' can't free them. */
  memset (ci_copy->children, 0,
      ci_copy->children_num * sizeof (*ci_copy->children));
  ci_copy->children_num = 0;

  oconfig_free (ci_copy);

  return (0);
} /* }}} int cjni_config_callback */

/* Free the data contained in the `user_data_t' pointer passed to `cjni_read'
 * and `cjni_write'. In particular, delete the global reference to the Java
 * object. */
static void cjni_callback_info_destroy (void *arg) /* {{{ */
{
  JNIEnv *jvm_env;
  cjni_callback_info_t *cbi;

  DEBUG ("java plugin: cjni_callback_info_destroy (arg = %p);", arg);

  cbi = (cjni_callback_info_t *) arg;

  /* This condition can occurr when shutting down. */
  if (jvm == NULL)
  {
    sfree (cbi);
    return;
  }

  if (arg == NULL)
    return;

  jvm_env = cjni_thread_attach ();
  if (jvm_env == NULL)
  {
    ERROR ("java plugin: cjni_callback_info_destroy: cjni_thread_attach failed.");
    return;
  }

  (*jvm_env)->DeleteGlobalRef (jvm_env, cbi->object);

  cbi->method = NULL;
  cbi->object = NULL;
  cbi->class  = NULL;
  free (cbi);

  cjni_thread_detach ();
} /* }}} void cjni_callback_info_destroy */

/* Call the CB_TYPE_READ callback pointed to by the `user_data_t' pointer. */
static int cjni_read (user_data_t *ud) /* {{{ */
{
  JNIEnv *jvm_env;
  cjni_callback_info_t *cbi;
  int status;
  int ret_status;

  if (jvm == NULL)
  {
    ERROR ("java plugin: cjni_read: jvm == NULL");
    return (-1);
  }

  if ((ud == NULL) || (ud->data == NULL))
  {
    ERROR ("java plugin: cjni_read: Invalid user data.");
    return (-1);
  }

  jvm_env = cjni_thread_attach ();
  if (jvm_env == NULL)
    return (-1);

  cbi = (cjni_callback_info_t *) ud->data;

  ret_status = (*jvm_env)->CallIntMethod (jvm_env, cbi->object,
      cbi->method);

  status = cjni_thread_detach ();
  if (status != 0)
  {
    ERROR ("java plugin: cjni_read: cjni_thread_detach failed.");
    return (-1);
  }

  return (ret_status);
} /* }}} int cjni_read */

/* Call the CB_TYPE_WRITE callback pointed to by the `user_data_t' pointer. */
static int cjni_write (const data_set_t *ds, const value_list_t *vl, /* {{{ */
    user_data_t *ud)
{
  JNIEnv *jvm_env;
  cjni_callback_info_t *cbi;
  jobject vl_java;
  int status;
  int ret_status;

  if (jvm == NULL)
  {
    ERROR ("java plugin: cjni_write: jvm == NULL");
    return (-1);
  }

  if ((ud == NULL) || (ud->data == NULL))
  {
    ERROR ("java plugin: cjni_write: Invalid user data.");
    return (-1);
  }

  jvm_env = cjni_thread_attach ();
  if (jvm_env == NULL)
    return (-1);

  cbi = (cjni_callback_info_t *) ud->data;

  vl_java = ctoj_value_list (jvm_env, ds, vl);
  if (vl_java == NULL)
  {
    ERROR ("java plugin: cjni_write: ctoj_value_list failed.");
    return (-1);
  }

  ret_status = (*jvm_env)->CallIntMethod (jvm_env,
      cbi->object, cbi->method, vl_java);

  (*jvm_env)->DeleteLocalRef (jvm_env, vl_java);

  status = cjni_thread_detach ();
  if (status != 0)
  {
    ERROR ("java plugin: cjni_write: cjni_thread_detach failed.");
    return (-1);
  }

  return (ret_status);
} /* }}} int cjni_write */

/* Call the CB_TYPE_FLUSH callback pointed to by the `user_data_t' pointer. */
static int cjni_flush (cdtime_t timeout, const char *identifier, /* {{{ */
    user_data_t *ud)
{
  JNIEnv *jvm_env;
  cjni_callback_info_t *cbi;
  jobject o_timeout;
  jobject o_identifier;
  int status;
  int ret_status;

  if (jvm == NULL)
  {
    ERROR ("java plugin: cjni_flush: jvm == NULL");
    return (-1);
  }

  if ((ud == NULL) || (ud->data == NULL))
  {
    ERROR ("java plugin: cjni_flush: Invalid user data.");
    return (-1);
  }

  jvm_env = cjni_thread_attach ();
  if (jvm_env == NULL)
    return (-1);

  cbi = (cjni_callback_info_t *) ud->data;

  o_timeout = ctoj_jdouble_to_number (jvm_env,
      (jdouble) CDTIME_T_TO_DOUBLE (timeout));
  if (o_timeout == NULL)
  {
    ERROR ("java plugin: cjni_flush: Converting double "
        "to Number object failed.");
    return (-1);
  }

  o_identifier = NULL;
  if (identifier != NULL)
  {
    o_identifier = (*jvm_env)->NewStringUTF (jvm_env, identifier);
    if (o_identifier == NULL)
    {
      (*jvm_env)->DeleteLocalRef (jvm_env, o_timeout);
      ERROR ("java plugin: cjni_flush: NewStringUTF failed.");
      return (-1);
    }
  }

  ret_status = (*jvm_env)->CallIntMethod (jvm_env,
      cbi->object, cbi->method, o_timeout, o_identifier);

  (*jvm_env)->DeleteLocalRef (jvm_env, o_identifier);
  (*jvm_env)->DeleteLocalRef (jvm_env, o_timeout);

  status = cjni_thread_detach ();
  if (status != 0)
  {
    ERROR ("java plugin: cjni_flush: cjni_thread_detach failed.");
    return (-1);
  }

  return (ret_status);
} /* }}} int cjni_flush */

/* Call the CB_TYPE_LOG callback pointed to by the `user_data_t' pointer. */
static void cjni_log (int severity, const char *message, /* {{{ */
    user_data_t *ud)
{
  JNIEnv *jvm_env;
  cjni_callback_info_t *cbi;
  jobject o_message;

  if (jvm == NULL)
    return;

  if ((ud == NULL) || (ud->data == NULL))
    return;

  jvm_env = cjni_thread_attach ();
  if (jvm_env == NULL)
    return;

  cbi = (cjni_callback_info_t *) ud->data;

  o_message = (*jvm_env)->NewStringUTF (jvm_env, message);
  if (o_message == NULL)
    return;

  (*jvm_env)->CallVoidMethod (jvm_env,
      cbi->object, cbi->method, (jint) severity, o_message);

  (*jvm_env)->DeleteLocalRef (jvm_env, o_message);

  cjni_thread_detach ();
} /* }}} void cjni_log */

/* Call the CB_TYPE_NOTIFICATION callback pointed to by the `user_data_t'
 * pointer. */
static int cjni_notification (const notification_t *n, /* {{{ */
    user_data_t *ud)
{
  JNIEnv *jvm_env;
  cjni_callback_info_t *cbi;
  jobject o_notification;
  int status;
  int ret_status;

  if (jvm == NULL)
  {
    ERROR ("java plugin: cjni_read: jvm == NULL");
    return (-1);
  }

  if ((ud == NULL) || (ud->data == NULL))
  {
    ERROR ("java plugin: cjni_read: Invalid user data.");
    return (-1);
  }

  jvm_env = cjni_thread_attach ();
  if (jvm_env == NULL)
    return (-1);

  cbi = (cjni_callback_info_t *) ud->data;

  o_notification = ctoj_notification (jvm_env, n);
  if (o_notification == NULL)
  {
    ERROR ("java plugin: cjni_notification: ctoj_notification failed.");
    return (-1);
  }

  ret_status = (*jvm_env)->CallIntMethod (jvm_env,
      cbi->object, cbi->method, o_notification);

  (*jvm_env)->DeleteLocalRef (jvm_env, o_notification);

  status = cjni_thread_detach ();
  if (status != 0)
  {
    ERROR ("java plugin: cjni_read: cjni_thread_detach failed.");
    return (-1);
  }

  return (ret_status);
} /* }}} int cjni_notification */

/* Callbacks for matches implemented in Java */
static int cjni_match_target_create (const oconfig_item_t *ci, /* {{{ */
    void **user_data)
{
  JNIEnv *jvm_env;
  cjni_callback_info_t *cbi_ret;
  cjni_callback_info_t *cbi_factory;
  const char *name;
  jobject o_ci;
  jobject o_tmp;
  int type;
  size_t i;

  cbi_ret = NULL;
  o_ci = NULL;
  jvm_env = NULL;

#define BAIL_OUT(status) \
  if (cbi_ret != NULL) { \
    free (cbi_ret->name); \
    if ((jvm_env != NULL) && (cbi_ret->object != NULL)) \
      (*jvm_env)->DeleteLocalRef (jvm_env, cbi_ret->object); \
  } \
  free (cbi_ret); \
  if (jvm_env != NULL) { \
    if (o_ci != NULL) \
      (*jvm_env)->DeleteLocalRef (jvm_env, o_ci); \
    cjni_thread_detach (); \
  } \
  return (status)

  if (jvm == NULL)
  {
    ERROR ("java plugin: cjni_read: jvm == NULL");
    BAIL_OUT (-1);
  }

  jvm_env = cjni_thread_attach ();
  if (jvm_env == NULL)
  {
    BAIL_OUT (-1);
  }

  /* Find out whether to create a match or a target. */
  if (strcasecmp ("Match", ci->key) == 0)
    type = CB_TYPE_MATCH;
  else if (strcasecmp ("Target", ci->key) == 0)
    type = CB_TYPE_TARGET;
  else
  {
    ERROR ("java plugin: cjni_match_target_create: Can't figure out whether "
        "to create a match or a target.");
    BAIL_OUT (-1);
  }

  /* This is the name of the match we should create. */
  name = ci->values[0].value.string;

  /* Lets see if we have a matching factory here.. */
  cbi_factory = NULL;
  for (i = 0; i < java_callbacks_num; i++)
  {
    if (java_callbacks[i].type != type)
      continue;

    if (strcmp (name, java_callbacks[i].name) != 0)
      continue;

    cbi_factory = java_callbacks + i;
    break;
  }

  /* Nope, no factory for that name.. */
  if (cbi_factory == NULL)
  {
    ERROR ("java plugin: cjni_match_target_create: "
        "No such match factory registered: %s",
        name);
    BAIL_OUT (-1);
  }

  /* We convert `ci' to its Java equivalent.. */
  o_ci = ctoj_oconfig_item (jvm_env, ci);
  if (o_ci == NULL)
  {
    ERROR ("java plugin: cjni_match_target_create: "
        "ctoj_oconfig_item failed.");
    BAIL_OUT (-1);
  }

  /* Allocate a new callback info structure. This is going to be our user_data
   * pointer. */
  cbi_ret = (cjni_callback_info_t *) malloc (sizeof (*cbi_ret));
  if (cbi_ret == NULL)
  {
    ERROR ("java plugin: cjni_match_target_create: malloc failed.");
    BAIL_OUT (-1);
  }
  memset (cbi_ret, 0, sizeof (*cbi_ret));
  cbi_ret->object = NULL;
  cbi_ret->type = type;

  /* Lets fill the callback info structure.. First, the name: */
  cbi_ret->name = strdup (name);
  if (cbi_ret->name == NULL)
  {
    ERROR ("java plugin: cjni_match_target_create: strdup failed.");
    BAIL_OUT (-1);
  }

  /* Then call the factory method so it creates a new object for us. */
  o_tmp = (*jvm_env)->CallObjectMethod (jvm_env,
      cbi_factory->object, cbi_factory->method, o_ci);
  if (o_tmp == NULL)
  {
    ERROR ("java plugin: cjni_match_target_create: CallObjectMethod failed.");
    BAIL_OUT (-1);
  }

  cbi_ret->object = (*jvm_env)->NewGlobalRef (jvm_env, o_tmp);
  if (o_tmp == NULL)
  {
    ERROR ("java plugin: cjni_match_target_create: NewGlobalRef failed.");
    BAIL_OUT (-1);
  }

  /* This is the class of the match. It is possibly different from the class of
   * the match-factory! */
  cbi_ret->class = (*jvm_env)->GetObjectClass (jvm_env, cbi_ret->object);
  if (cbi_ret->class == NULL)
  {
    ERROR ("java plugin: cjni_match_target_create: GetObjectClass failed.");
    BAIL_OUT (-1);
  }

  /* Lookup the `int match (DataSet, ValueList)' method. */
  cbi_ret->method = (*jvm_env)->GetMethodID (jvm_env, cbi_ret->class,
      /* method name = */ (type == CB_TYPE_MATCH) ? "match" : "invoke",
      "(Lorg/collectd/api/DataSet;Lorg/collectd/api/ValueList;)I");
  if (cbi_ret->method == NULL)
  {
    ERROR ("java plugin: cjni_match_target_create: GetMethodID failed.");
    BAIL_OUT (-1);
  }

  /* Return the newly created match via the user_data pointer. */
  *user_data = (void *) cbi_ret;

  cjni_thread_detach ();

  DEBUG ("java plugin: cjni_match_target_create: "
      "Successfully created a `%s' %s.",
      cbi_ret->name, (type == CB_TYPE_MATCH) ? "match" : "target");

  /* Success! */
  return (0);
#undef BAIL_OUT
} /* }}} int cjni_match_target_create */

static int cjni_match_target_destroy (void **user_data) /* {{{ */
{
  cjni_callback_info_destroy (*user_data);
  *user_data = NULL;

  return (0);
} /* }}} int cjni_match_target_destroy */

static int cjni_match_target_invoke (const data_set_t *ds, /* {{{ */
    value_list_t *vl, notification_meta_t **meta, void **user_data)
{
  JNIEnv *jvm_env;
  cjni_callback_info_t *cbi;
  jobject o_vl;
  jobject o_ds;
  int ret_status;
  int status;

  if (jvm == NULL)
  {
    ERROR ("java plugin: cjni_match_target_invoke: jvm == NULL");
    return (-1);
  }

  jvm_env = cjni_thread_attach ();
  if (jvm_env == NULL)
    return (-1);

  cbi = (cjni_callback_info_t *) *user_data;

  o_vl = ctoj_value_list (jvm_env, ds, vl);
  if (o_vl == NULL)
  {
    ERROR ("java plugin: cjni_match_target_invoke: ctoj_value_list failed.");
    cjni_thread_detach ();
    return (-1);
  }

  o_ds = ctoj_data_set (jvm_env, ds);
  if (o_ds == NULL)
  {
    ERROR ("java plugin: cjni_match_target_invoke: ctoj_value_list failed.");
    cjni_thread_detach ();
    return (-1);
  }

  ret_status = (*jvm_env)->CallIntMethod (jvm_env, cbi->object, cbi->method,
      o_ds, o_vl);

  DEBUG ("java plugin: cjni_match_target_invoke: Method returned %i.", ret_status);

  /* If we're executing a target, copy the `ValueList' back to our
   * `value_list_t'. */
  if (cbi->type == CB_TYPE_TARGET)
  {
    value_list_t new_vl;

    memset (&new_vl, 0, sizeof (new_vl));
    status = jtoc_value_list (jvm_env, &new_vl, o_vl);
    if (status != 0)
    {
      ERROR ("java plugin: cjni_match_target_invoke: "
          "jtoc_value_list failed.");
    }
    else /* if (status == 0) */
    {
      /* plugin_dispatch_values assures that this is dynamically allocated
       * memory. */
      sfree (vl->values);

      /* This will replace the vl->values pointer to a new, dynamically
       * allocated piece of memory. */
      memcpy (vl, &new_vl, sizeof (*vl));
    }
  } /* if (cbi->type == CB_TYPE_TARGET) */

  status = cjni_thread_detach ();
  if (status != 0)
    ERROR ("java plugin: cjni_read: cjni_thread_detach failed.");

  return (ret_status);
} /* }}} int cjni_match_target_invoke */

/* Iterate over `java_callbacks' and call all CB_TYPE_INIT callbacks. */
static int cjni_init_plugins (JNIEnv *jvm_env) /* {{{ */
{
  int status;
  size_t i;

  for (i = 0; i < java_callbacks_num; i++)
  {
    if (java_callbacks[i].type != CB_TYPE_INIT)
      continue;

    DEBUG ("java plugin: Initializing %s", java_callbacks[i].name);

    status = (*jvm_env)->CallIntMethod (jvm_env,
        java_callbacks[i].object, java_callbacks[i].method);
    if (status != 0)
    {
      ERROR ("java plugin: Initializing `%s' failed with status %i. "
          "Removing read function.",
          java_callbacks[i].name, status);
      plugin_unregister_read (java_callbacks[i].name);
    }
  }

  return (0);
} /* }}} int cjni_init_plugins */

/* Iterate over `java_callbacks' and call all CB_TYPE_SHUTDOWN callbacks. */
static int cjni_shutdown_plugins (JNIEnv *jvm_env) /* {{{ */
{
  int status;
  size_t i;

  for (i = 0; i < java_callbacks_num; i++)
  {
    if (java_callbacks[i].type != CB_TYPE_SHUTDOWN)
      continue;

    DEBUG ("java plugin: Shutting down %s", java_callbacks[i].name);

    status = (*jvm_env)->CallIntMethod (jvm_env,
        java_callbacks[i].object, java_callbacks[i].method);
    if (status != 0)
    {
      ERROR ("java plugin: Shutting down `%s' failed with status %i. ",
          java_callbacks[i].name, status);
    }
  }

  return (0);
} /* }}} int cjni_shutdown_plugins */


static int cjni_shutdown (void) /* {{{ */
{
  JNIEnv *jvm_env;
  JavaVMAttachArgs args;
  int status;
  size_t i;

  if (jvm == NULL)
    return (0);

  jvm_env = NULL;
  memset (&args, 0, sizeof (args));
  args.version = JNI_VERSION_1_2;

  status = (*jvm)->AttachCurrentThread (jvm, (void *) &jvm_env, &args);
  if (status != 0)
  {
    ERROR ("java plugin: cjni_shutdown: AttachCurrentThread failed with status %i.",
        status);
    return (-1);
  }

  /* Execute all the shutdown functions registered by plugins. */
  cjni_shutdown_plugins (jvm_env);

  /* Release all the global references to callback functions */
  for (i = 0; i < java_callbacks_num; i++)
  {
    if (java_callbacks[i].object != NULL)
    {
      (*jvm_env)->DeleteGlobalRef (jvm_env, java_callbacks[i].object);
      java_callbacks[i].object = NULL;
    }
    sfree (java_callbacks[i].name);
  }
  java_callbacks_num = 0;
  sfree (java_callbacks);

  /* Release all the global references to directly loaded classes. */
  for (i = 0; i < java_classes_list_len; i++)
  {
    if (java_classes_list[i].object != NULL)
    {
      (*jvm_env)->DeleteGlobalRef (jvm_env, java_classes_list[i].object);
      java_classes_list[i].object = NULL;
    }
    sfree (java_classes_list[i].name);
  }
  java_classes_list_len = 0;
  sfree (java_classes_list);

  /* Destroy the JVM */
  DEBUG ("java plugin: Destroying the JVM.");
  (*jvm)->DestroyJavaVM (jvm);
  jvm = NULL;
  jvm_env = NULL;

  pthread_key_delete (jvm_env_key);

  /* Free the JVM argument list */
  for (i = 0; i < jvm_argc; i++)
    sfree (jvm_argv[i]);
  jvm_argc = 0;
  sfree (jvm_argv);

  return (0);
} /* }}} int cjni_shutdown */

/* Initialization: Create a JVM, load all configured classes and call their
 * `config' and `init' callback methods. */
static int cjni_init (void) /* {{{ */
{
  JNIEnv *jvm_env;

  if ((config_block == NULL) && (jvm == NULL))
  {
    ERROR ("java plugin: cjni_init: No configuration block for "
        "the java plugin was found.");
    return (-1);
  }

  if (config_block != NULL)
  {

    cjni_config_perform (config_block);
    oconfig_free (config_block);
    config_block = NULL;
  }

  if (jvm == NULL)
  {
    ERROR ("java plugin: cjni_init: jvm == NULL");
    return (-1);
  }

  jvm_env = cjni_thread_attach ();
  if (jvm_env == NULL)
    return (-1);

  cjni_init_plugins (jvm_env);

  cjni_thread_detach ();
  return (0);
} /* }}} int cjni_init */

void module_register (void)
{
  plugin_register_complex_config ("java", cjni_config_callback);
  plugin_register_init ("java", cjni_init);
  plugin_register_shutdown ("java", cjni_shutdown);
} /* void module_register (void) */

/* vim: set sw=2 sts=2 et fdm=marker : */
