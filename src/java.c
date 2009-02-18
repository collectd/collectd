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

  oconfig_item_t *ci;

#define CJNI_FLAG_ENABLED 0x0001
  int flags;

  jmethodID m_config;
  jmethodID m_init;
  jmethodID m_read;
  jmethodID m_write;
  jmethodID m_shutdown;
};
typedef struct java_plugin_s java_plugin_t;
/* }}} */

/*
 * Global variables
 */
static JavaVM *jvm = NULL;

static char **jvm_argv = NULL;
static size_t jvm_argc = 0;

static java_plugin_t *java_plugins     = NULL;
static size_t         java_plugins_num = 0;

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

  DEBUG ("java plugin: ctoj_string: ->%s (%s);",
      method_name, (string != NULL) ? string : "");

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

  DEBUG ("java plugin: ctoj_int: ->%s (%i);",
      method_name, (int) value);

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

  DEBUG ("java plugin: ctoj_long: ->%s (%"PRIi64");",
      method_name, (int64_t) value);

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

  DEBUG ("java plugin: ctoj_double: ->%s (%g);",
      method_name, (double) value);

  return (0);
} /* }}} int ctoj_double */

/* Convert a jlong to a java.lang.Number */
static jobject ctoj_jlong_to_number (JNIEnv *jvm_env, jlong value) /* {{{ */
{
  jclass c_long;
  jmethodID m_long_constructor;

  /* Look up the java.lang.Long class */
  c_long = (*jvm_env)->FindClass (jvm_env, "java.lang.Long");
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
  c_double = (*jvm_env)->FindClass (jvm_env, "java.lang.Double");
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
  else
    return (NULL);
} /* }}} jobject ctoj_value_to_number */

/* Convert a data_source_t to a org.collectd.protocol.DataSource */
static jobject ctoj_data_source (JNIEnv *jvm_env, /* {{{ */
    const data_source_t *dsrc)
{
  jclass c_datasource;
  jmethodID m_datasource_constructor;
  jobject o_datasource;
  int status;

  /* Look up the DataSource class */
  c_datasource = (*jvm_env)->FindClass (jvm_env,
      "org.collectd.protocol.DataSource");
  if (c_datasource == NULL)
  {
    ERROR ("java plugin: ctoj_data_source: "
        "FindClass (org.collectd.protocol.DataSource) failed.");
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

/* Convert a oconfig_value_t to a org.collectd.api.OConfigValue */
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
      "org.collectd.api.OConfigValue");
  if (c_ocvalue == NULL)
  {
    ERROR ("java plugin: ctoj_oconfig_value: "
        "FindClass (org.collectd.api.OConfigValue) failed.");
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

/* Convert a oconfig_item_t to a org.collectd.api.OConfigItem */
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

  c_ocitem = (*jvm_env)->FindClass (jvm_env, "org.collectd.api.OConfigItem");
  if (c_ocitem == NULL)
  {
    ERROR ("java plugin: ctoj_oconfig_item: "
        "FindClass (org.collectd.api.OConfigItem) failed.");
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

    (*jvm_env)->CallVoidMethod (jvm_env, o_ocitem, m_addvalue, o_child);
    (*jvm_env)->DeleteLocalRef (jvm_env, o_child);
  } /* }}} for (i = 0; i < ci->children_num; i++) */

  return (o_ocitem);
} /* }}} jobject ctoj_oconfig_item */

/* Convert a data_set_t to a java.util.List<DataSource> */
static jobject ctoj_data_set (JNIEnv *jvm_env, const data_set_t *ds) /* {{{ */
{
  jclass c_arraylist;
  jmethodID m_constructor;
  jmethodID m_add;
  jobject o_dataset;
  int i;

  /* Look up the java.util.ArrayList class */
  c_arraylist = (*jvm_env)->FindClass (jvm_env, "java.util.ArrayList");
  if (c_arraylist == NULL)
  {
    ERROR ("java plugin: ctoj_data_set: Looking up the "
        "java.util.ArrayList class failed.");
    return (NULL);
  }

  /* Search for the `ArrayList (int capacity)' constructor. */
  m_constructor = (*jvm_env)->GetMethodID (jvm_env,
      c_arraylist, "<init>", "()V");
  if (m_constructor == NULL)
  {
    ERROR ("java plugin: ctoj_data_set: Looking up the "
        "`ArrayList (void)' constructor failed.");
    return (NULL);
  }

  /* Search for the `boolean add  (Object element)' method. */
  m_add = (*jvm_env)->GetMethodID (jvm_env,
      c_arraylist, "add", "(Ljava/lang/Object;)Z");
  if (m_add == NULL)
  {
    ERROR ("java plugin: ctoj_data_set: Looking up the "
        "`add (Object)' method failed.");
    return (NULL);
  }

  o_dataset = (*jvm_env)->NewObject (jvm_env, c_arraylist, m_constructor);
  if (o_dataset == NULL)
  {
    ERROR ("java plugin: ctoj_data_set: "
        "Creating an ArrayList object failed.");
    return (NULL);
  }

  for (i = 0; i < ds->ds_num; i++)
  {
    jobject o_datasource;
    jboolean status;

    o_datasource = ctoj_data_source (jvm_env, ds->ds + i);
    if (o_datasource == NULL)
    {
      ERROR ("java plugin: ctoj_data_set: ctoj_data_source (%s.%s) failed",
          ds->type, ds->ds[i].name);
      (*jvm_env)->DeleteLocalRef (jvm_env, o_dataset);
      return (NULL);
    }

    status = (*jvm_env)->CallBooleanMethod (jvm_env,
        o_dataset, m_add, o_datasource);
    if (!status)
    {
      ERROR ("java plugin: ctoj_data_set: ArrayList.add returned FALSE.");
      (*jvm_env)->DeleteLocalRef (jvm_env, o_datasource);
      (*jvm_env)->DeleteLocalRef (jvm_env, o_dataset);
      return (NULL);
    }

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
  jmethodID m_setdatasource;
  jobject o_dataset;

  /* Look for the `void setDataSource (List<DataSource> ds)' method. */
  m_setdatasource = (*jvm_env)->GetMethodID (jvm_env, c_valuelist,
      "setDataSource", "(Ljava/util/List;)V");
  if (m_setdatasource == NULL)
  {
    ERROR ("java plugin: ctoj_value_list_add_data_set: "
        "Cannot find the `void setDataSource (List<DataSource> ds)' method.");
    return (-1);
  }

  /* Create a List<DataSource> object. */
  o_dataset = ctoj_data_set (jvm_env, ds);
  if (o_dataset == NULL)
  {
    ERROR ("java plugin: ctoj_value_list_add_data_set: "
        "ctoj_data_set (%s) failed.", ds->type);
    return (-1);
  }

  /* Actually call the method. */
  (*jvm_env)->CallVoidMethod (jvm_env,
      o_valuelist, m_setdatasource, o_dataset);

  /* Decrease reference counter on the List<DataSource> object. */
  (*jvm_env)->DeleteLocalRef (jvm_env, o_dataset);

  return (0);
} /* }}} int ctoj_value_list_add_data_set */

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
      "org.collectd.protocol.ValueList");
  if (c_valuelist == NULL)
  {
    ERROR ("java plugin: ctoj_value_list: "
        "FindClass (org.collectd.protocol.ValueList) failed.");
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
    ERROR ("java plugin: ctoj_value_list: jtoc_string (%s) failed.", \
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
  status = ctoj_long (jvm_env, ((jlong) vl->time) * ((jlong) 1000),
      c_valuelist, o_valuelist, "setTime");
  if (status != 0)
  {
    ERROR ("java plugin: ctoj_value_list: ctoj_long (setTime) failed.");
    (*jvm_env)->DeleteLocalRef (jvm_env, o_valuelist);
    return (NULL);
  }

  /* Set the `interval' member.. */
  status = ctoj_long (jvm_env, (jlong) vl->interval,
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
} /* }}} int ctoj_value_list */

/*
 * Java to C conversion functions
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
    ERROR ("java plugin: jtoc_long: Cannot find method `long %s ()'.",
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

/* Convert a org.collectd.protocol.ValueList to a value_list_t. */
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
    ERROR ("java plugin: jtoc_value_list: jtoc_long (getTime) failed.");
    return (-1);
  }
  vl->time = (time_t) tmp_long;

  status = jtoc_long (jvm_env, &tmp_long,
      class_ptr, object_ptr, "getInterval");
  if (status != 0)
  {
    ERROR ("java plugin: jtoc_value_list: jtoc_long (getInterval) failed.");
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

  status = plugin_dispatch_values (&vl);

  sfree (vl.values);

  return (status);
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
static int cjni_config_add_jvm_arg (oconfig_item_t *ci) /* {{{ */
{
  char **tmp;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("java plugin: `JVMArg' needs exactly one string argument.");
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
  java_plugin_t *jp;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("java plugin: `LoadPlugin' needs exactly one string argument.");
    return (-1);
  }

  jp = (java_plugin_t *) realloc (java_plugins,
      sizeof (*java_plugins) * (java_plugins_num + 1));
  if (jp == NULL)
  {
    ERROR ("java plugin: realloc failed.");
    return (-1);
  }
  java_plugins = jp;
  jp = java_plugins + java_plugins_num;

  memset (jp, 0, sizeof (*jp));
  jp->class_name = strdup (ci->values[0].value.string);
  if (jp->class_name == NULL)
  {
    ERROR ("java plugin: strdup failed.");
    return (-1);
  }

  jp->class_ptr  = NULL;
  jp->object_ptr = NULL;
  jp->ci         = NULL;
  jp->flags      = 0;
  jp->m_config   = NULL;
  jp->m_init     = NULL;
  jp->m_read     = NULL;
  jp->m_write    = NULL;
  jp->m_shutdown = NULL;

  java_plugins_num++;

  return (0);
} /* }}} int cjni_config_load_plugin */

static int cjni_config_plugin_block (oconfig_item_t *ci) /* {{{ */
{
  size_t i;
  const char *class_name;

  if ((ci->values_num != 1) || (ci->values[0].type != OCONFIG_TYPE_STRING))
  {
    WARNING ("java plugin: `Plugin' blocks "
        "need exactly one string argument.");
    return (-1);
  }

  class_name = ci->values[0].value.string;
  for (i = 0; i < java_plugins_num; i++)
    if (strcmp (java_plugins[i].class_name, class_name) == 0)
      break;

  if (i >= java_plugins_num)
  {
    WARNING ("java plugin: Configuration block for the `%s' plugin found, "
        "but the plugin has not been loaded. Please note, that the class "
        "name is case-sensitive!",
        class_name);
    return (0);
  }

  if (java_plugins[i].ci != NULL)
  {
    WARNING ("java plugin: There are more than one <Plugin> blocks for the "
        "`%s' plugin. This is currently not supported - only the first block "
        "will be used!",
        class_name);
    return (0);
  }

  java_plugins[i].ci = oconfig_clone (ci);
  if (java_plugins[i].ci == NULL)
  {
    ERROR ("java plugin: cjni_config_plugin_block: "
        "oconfig_clone failed for `%s'.",
        class_name);
    return (-1);
  }

  DEBUG ("java plugin: cjni_config_plugin_block: "
      "Successfully copied config for `%s'.",
      class_name);

  return (0);
} /* }}} int cjni_config_plugin_block */

static int cjni_config (oconfig_item_t *ci) /* {{{ */
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

  if ((success == 0) && (errors > 0))
  {
    ERROR ("java plugin: All statements failed.");
    return (-1);
  }

  return (0);
} /* }}} int cjni_config */

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

  jp->m_config = (*jvm_env)->GetMethodID (jvm_env, jp->class_ptr,
      "Config", "(Lorg/collectd/api/OConfigItem;)I");
  DEBUG ("java plugin: cjni_init_one_plugin: "
      "jp->class_name = %s; jp->m_config = %p;",
      jp->class_name, (void *) jp->m_config);

  jp->m_init = (*jvm_env)->GetMethodID (jvm_env, jp->class_ptr,
      "Init", "()I");
  DEBUG ("java plugin: cjni_init_one_plugin: "
      "jp->class_name = %s; jp->m_init = %p;",
      jp->class_name, (void *) jp->m_init);

  jp->m_read = (*jvm_env)->GetMethodID (jvm_env, jp->class_ptr,
      "Read", "()I");
  DEBUG ("java plugin: cjni_init_one_plugin: "
      "jp->class_name = %s; jp->m_read = %p;",
      jp->class_name, (void *) jp->m_read);

  jp->m_write = (*jvm_env)->GetMethodID (jvm_env, jp->class_ptr,
      "Write", "(Lorg/collectd/protocol/ValueList;)I");
  DEBUG ("java plugin: cjni_init_one_plugin: "
      "jp->class_name = %s; jp->m_write = %p;",
      jp->class_name, (void *) jp->m_write);

  jp->m_shutdown = (*jvm_env)->GetMethodID (jvm_env, jp->class_ptr,
      "Shutdown", "()I");
  DEBUG ("java plugin: cjni_init_one_plugin: "
      "jp->class_name = %s; jp->m_shutdown = %p;",
      jp->class_name, (void *) jp->m_shutdown);

  if (jp->ci != NULL)
  {
    if (jp->m_config == NULL)
    {
      WARNING ("java plugin: Configuration for the `%s' plugin is present, "
          "but plugin doesn't provide a configuration method.",
          jp->class_name);
    }
    else
    {
      jobject o_ocitem;

      o_ocitem = ctoj_oconfig_item (jvm_env, jp->ci);
      if (o_ocitem == NULL)
      {
        ERROR ("java plugin: Creating an OConfigItem object failed. "
            "Can't pass configuration information to the `%s' plugin!",
            jp->class_name);
      }
      else
      {
        status = (*jvm_env)->CallIntMethod (jvm_env,
            jp->object_ptr, jp->m_config, o_ocitem);
        if (status != 0)
        {
          ERROR ("java plugin: cjni_init_one_plugin: "
              "Configuring the `%s' object failed with status %i.",
              jp->class_name, status);
        }
        (*jvm_env)->DeleteLocalRef (jvm_env, o_ocitem);
      }
    }
  } /* if (jp->ci != NULL) */

  if (jp->m_init != NULL)
  {
    status = (*jvm_env)->CallIntMethod (jvm_env, jp->object_ptr,
        jp->m_init);
    if (status != 0)
    {
      ERROR ("java plugin: cjni_init_one_plugin: "
        "Initializing `%s' object failed with status %i.",
        jp->class_name, status);
      return (-1);
    }
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

  api_class_ptr = (*jvm_env)->FindClass (jvm_env, "org.collectd.api.CollectdAPI");
  if (api_class_ptr == NULL)
  {
    ERROR ("cjni_init_native: Cannot find API class `org.collectd.api.CollectdAPI'.");
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
  JavaVMOption vm_options[jvm_argc];

  int status;
  size_t i;

  if (jvm != NULL)
    return (0);

  jvm_env = NULL;

  memset (&vm_args, 0, sizeof (vm_args));
  vm_args.version = JNI_VERSION_1_2;
  vm_args.options = vm_options;
  vm_args.nOptions = (jint) jvm_argc;

  for (i = 0; i < jvm_argc; i++)
  {
    DEBUG ("java plugin: cjni_init: jvm_argv[%zu] = %s", i, jvm_argv[i]);
    vm_args.options[i].optionString = jvm_argv[i];
  }
  /*
  vm_args.options[0].optionString = "-verbose:jni";
  vm_args.options[1].optionString = "-Djava.class.path=/home/octo/collectd/bindings/java";
  */

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
      || (jp->m_read == NULL))
    return (0);

  DEBUG ("java plugin: Calling: %s.Read()", jp->class_name);

  status = (*jvm_env)->CallIntMethod (jvm_env, jp->object_ptr,
      jp->m_read);
  if (status != 0)
  {
    ERROR ("java plugin: cjni_read_one_plugin: "
        "Calling `Read' on an `%s' object failed with status %i.",
        jp->class_name, status);
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

static int cjni_write_one_plugin (JNIEnv *jvm_env, /* {{{ */
    java_plugin_t *jp, jobject vl_java)
{
  int status;

  if ((jp == NULL)
      || ((jp->flags & CJNI_FLAG_ENABLED) == 0)
      || (jp->m_write == NULL))
    return (0);

  DEBUG ("java plugin: Calling: %s.Write(ValueList)", jp->class_name);

  status = (*jvm_env)->CallIntMethod (jvm_env, jp->object_ptr,
      jp->m_write, vl_java);
  if (status != 0)
  {
    ERROR ("java plugin: cjni_write_one_plugin: "
        "Calling `Write' on an `%s' object failed with status %i.",
        jp->class_name, status);
    return (-1);
  }

  return (0);
} /* }}} int cjni_write_one_plugin */

static int cjni_write_plugins (JNIEnv *jvm_env, /* {{{ */
    const data_set_t *ds, const value_list_t *vl)
{
  size_t j;

  jobject vl_java;

  vl_java = ctoj_value_list (jvm_env, ds, vl);
  if (vl_java == NULL)
  {
    ERROR ("java plugin: cjni_write_plugins: ctoj_value_list failed.");
    return (-1);
  }

  for (j = 0; j < java_plugins_num; j++)
    cjni_write_one_plugin (jvm_env, &java_plugins[j], vl_java);

  (*jvm_env)->DeleteLocalRef (jvm_env, vl_java);

  return (0);
} /* }}} int cjni_write_plugins */

static int cjni_write (const data_set_t *ds, const value_list_t *vl) /* {{{ */
{
  JNIEnv *jvm_env;
  JavaVMAttachArgs args;
  int status;

  if (jvm == NULL)
  {
    ERROR ("java plugin: cjni_write: jvm == NULL");
    return (-1);
  }

  jvm_env = NULL;
  memset (&args, 0, sizeof (args));
  args.version = JNI_VERSION_1_2;

  status = (*jvm)->AttachCurrentThread (jvm, (void **) &jvm_env, &args);
  if (status != 0)
  {
    ERROR ("java plugin: cjni_write: AttachCurrentThread failed with status %i.",
        status);
    return (-1);
  }

  cjni_write_plugins (jvm_env, ds, vl);

  status = (*jvm)->DetachCurrentThread (jvm);
  if (status != 0)
  {
    ERROR ("java plugin: cjni_write: DetachCurrentThread failed with status %i.",
        status);
    return (-1);
  }

  return (0);
} /* }}} int cjni_write */

static int cjni_shutdown_one_plugin (JNIEnv *jvm_env, /* {{{ */
    java_plugin_t *jp)
{
  int status;

  if ((jp == NULL)
      || ((jp->flags & CJNI_FLAG_ENABLED) == 0)
      || (jp->m_shutdown == NULL))
    return (0);

  status = (*jvm_env)->CallIntMethod (jvm_env, jp->object_ptr,
      jp->m_shutdown);
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
  size_t i;

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

  for (i = 0; i < jvm_argc; i++)
  {
    sfree (jvm_argv[i]);
  }
  sfree (jvm_argv);
  jvm_argc = 0;

  for (i = 0; i < java_plugins_num; i++)
  {
    sfree (java_plugins[i].class_name);
    oconfig_free (java_plugins[i].ci);
  }
  sfree (java_plugins);
  java_plugins_num = 0;

  return (0);
} /* }}} int cjni_shutdown */

void module_register (void)
{
  plugin_register_complex_config ("java", cjni_config);
  plugin_register_init ("java", cjni_init);
  plugin_register_read ("java", cjni_read);
  plugin_register_write ("java", cjni_write);
  plugin_register_shutdown ("java", cjni_shutdown);
} /* void module_register (void) */

/* vim: set sw=2 sts=2 et fdm=marker : */
