/**
 * collectd - src/pyvalues.c
 * Copyright (C) 2009  Sven Trenkel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Sven Trenkel <collectd at semidefinite.de>  
 **/

#include <Python.h>
#include <structmember.h>

#include "collectd.h"
#include "common.h"

#include "cpython.h"

static PyObject *cpy_common_repr(PyObject *s) {
	PyObject *ret, *tmp;
	static PyObject *l_type = NULL, *l_type_instance = NULL, *l_plugin = NULL, *l_plugin_instance = NULL;
	static PyObject *l_host = NULL, *l_time = NULL;
	PluginData *self = (PluginData *) s;
	
	if (l_type == NULL)
		l_type = cpy_string_to_unicode_or_bytes("(type=");
	if (l_type_instance == NULL)
		l_type_instance = cpy_string_to_unicode_or_bytes(",type_instance=");
	if (l_plugin == NULL)
		l_plugin = cpy_string_to_unicode_or_bytes(",plugin=");
	if (l_plugin_instance == NULL)
		l_plugin_instance = cpy_string_to_unicode_or_bytes(",plugin_instance=");
	if (l_host == NULL)
		l_host = cpy_string_to_unicode_or_bytes(",host=");
	if (l_time == NULL)
		l_time = cpy_string_to_unicode_or_bytes(",time=");
	
	if (!l_type || !l_type_instance || !l_plugin || !l_plugin_instance || !l_host || !l_time)
		return NULL;
	
	ret = cpy_string_to_unicode_or_bytes(s->ob_type->tp_name);

	CPY_STRCAT(&ret, l_type);
	tmp = cpy_string_to_unicode_or_bytes(self->type);
	CPY_SUBSTITUTE(PyObject_Repr, tmp, tmp);
	CPY_STRCAT_AND_DEL(&ret, tmp);

	if (self->type_instance[0] != 0) {
		CPY_STRCAT(&ret, l_type_instance);
		tmp = cpy_string_to_unicode_or_bytes(self->type_instance);
		CPY_SUBSTITUTE(PyObject_Repr, tmp, tmp);
		CPY_STRCAT_AND_DEL(&ret, tmp);
	}

	if (self->plugin[0] != 0) {
		CPY_STRCAT(&ret, l_plugin);
		tmp = cpy_string_to_unicode_or_bytes(self->plugin);
		CPY_SUBSTITUTE(PyObject_Repr, tmp, tmp);
		CPY_STRCAT_AND_DEL(&ret, tmp);
	}

	if (self->plugin_instance[0] != 0) {
		CPY_STRCAT(&ret, l_plugin_instance);
		tmp = cpy_string_to_unicode_or_bytes(self->plugin_instance);
		CPY_SUBSTITUTE(PyObject_Repr, tmp, tmp);
		CPY_STRCAT_AND_DEL(&ret, tmp);
	}

	if (self->host[0] != 0) {
		CPY_STRCAT(&ret, l_host);
		tmp = cpy_string_to_unicode_or_bytes(self->host);
		CPY_SUBSTITUTE(PyObject_Repr, tmp, tmp);
		CPY_STRCAT_AND_DEL(&ret, tmp);
	}

	if (self->time != 0) {
		CPY_STRCAT(&ret, l_time);
		tmp = PyFloat_FromDouble(self->time);
		CPY_SUBSTITUTE(PyObject_Repr, tmp, tmp);
		CPY_STRCAT_AND_DEL(&ret, tmp);
	}
	return ret;
}

static char time_doc[] = "This is the Unix timestap of the time this value was read.\n"
		"For dispatching values this can be set to 0 which means \"now\".\n"
		"This means the time the value is actually dispatched, not the time\n"
		"it was set to 0.";

static char host_doc[] = "The hostname of the host this value was read from.\n"
		"For dispatching this can be set to an empty string which means\n"
		"the local hostname as defined in the collectd.conf.";

static char type_doc[] = "The type of this value. This type has to be defined\n"
		"in your types.db. Attempting to set it to any other value will\n"
		"raise a TypeError exception.\n"
		"Assigning a type is mandetory, calling dispatch without doing\n"
		"so will raise a RuntimeError exception.";

static char type_instance_doc[] = "";

static char plugin_doc[] = "The name of the plugin that read the data. Setting this\n"
		"member to an empty string will insert \"python\" upon dispatching.";

static char plugin_instance_doc[] = "";

static char PluginData_doc[] = "This is an internal class that is the base for Values\n"
		"and Notification. It is pretty useless by itself and was therefore not\n"
		"exported to the collectd module.";

static PyObject *PluginData_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
	PluginData *self;
	
	self = (PluginData *) type->tp_alloc(type, 0);
	if (self == NULL)
		return NULL;
	
	self->time = 0;
	self->host[0] = 0;
	self->plugin[0] = 0;
	self->plugin_instance[0] = 0;
	self->type[0] = 0;
	self->type_instance[0] = 0;
	return (PyObject *) self;
}

static int PluginData_init(PyObject *s, PyObject *args, PyObject *kwds) {
	PluginData *self = (PluginData *) s;
	double time = 0;
	const char *type = "", *plugin_instance = "", *type_instance = "", *plugin = "", *host = "";
	static char *kwlist[] = {"type", "plugin_instance", "type_instance",
			"plugin", "host", "time", NULL};
	
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|etetetetetd", kwlist, NULL, &type,
			NULL, &plugin_instance, NULL, &type_instance, NULL, &plugin, NULL, &host, &time))
		return -1;
	
	if (type[0] != 0 && plugin_get_ds(type) == NULL) {
		PyErr_Format(PyExc_TypeError, "Dataset %s not found", type);
		return -1;
	}

	sstrncpy(self->host, host, sizeof(self->host));
	sstrncpy(self->plugin, plugin, sizeof(self->plugin));
	sstrncpy(self->plugin_instance, plugin_instance, sizeof(self->plugin_instance));
	sstrncpy(self->type, type, sizeof(self->type));
	sstrncpy(self->type_instance, type_instance, sizeof(self->type_instance));
	
	self->time = time;
	return 0;
}

static PyObject *PluginData_repr(PyObject *s) {
	PyObject *ret;
	static PyObject *l_closing = NULL;
	
	if (l_closing == NULL)
		l_closing = cpy_string_to_unicode_or_bytes(")");
	
	if (l_closing == NULL)
		return NULL;
	
	ret = cpy_common_repr(s);
	CPY_STRCAT(&ret, l_closing);
	return ret;
}

static PyMemberDef PluginData_members[] = {
	{"time", T_DOUBLE, offsetof(PluginData, time), 0, time_doc},
	{NULL}
};

static PyObject *PluginData_getstring(PyObject *self, void *data) {
	const char *value = ((char *) self) + (intptr_t) data;
	
	return cpy_string_to_unicode_or_bytes(value);
}

static int PluginData_setstring(PyObject *self, PyObject *value, void *data) {
	char *old;
	const char *new;
	
	if (value == NULL) {
		PyErr_SetString(PyExc_TypeError, "Cannot delete this attribute");
		return -1;
	}
	Py_INCREF(value);
	new = cpy_unicode_or_bytes_to_string(&value);
	if (new == NULL) {
		Py_DECREF(value);
		return -1;
	}
	old = ((char *) self) + (intptr_t) data;
	sstrncpy(old, new, DATA_MAX_NAME_LEN);
	Py_DECREF(value);
	return 0;
}

static int PluginData_settype(PyObject *self, PyObject *value, void *data) {
	char *old;
	const char *new;
	
	if (value == NULL) {
		PyErr_SetString(PyExc_TypeError, "Cannot delete this attribute");
		return -1;
	}
	Py_INCREF(value);
	new = cpy_unicode_or_bytes_to_string(&value);
	if (new == NULL) {
		Py_DECREF(value);
		return -1;
	}

	if (plugin_get_ds(new) == NULL) {
		PyErr_Format(PyExc_TypeError, "Dataset %s not found", new);
		Py_DECREF(value);
		return -1;
	}

	old = ((char *) self) + (intptr_t) data;
	sstrncpy(old, new, DATA_MAX_NAME_LEN);
	Py_DECREF(value);
	return 0;
}

static PyGetSetDef PluginData_getseters[] = {
	{"host", PluginData_getstring, PluginData_setstring, host_doc, (void *) offsetof(PluginData, host)},
	{"plugin", PluginData_getstring, PluginData_setstring, plugin_doc, (void *) offsetof(PluginData, plugin)},
	{"plugin_instance", PluginData_getstring, PluginData_setstring, plugin_instance_doc, (void *) offsetof(PluginData, plugin_instance)},
	{"type_instance", PluginData_getstring, PluginData_setstring, type_instance_doc, (void *) offsetof(PluginData, type_instance)},
	{"type", PluginData_getstring, PluginData_settype, type_doc, (void *) offsetof(PluginData, type)},
	{NULL}
};

PyTypeObject PluginDataType = {
	CPY_INIT_TYPE
	"collectd.PluginData",     /* tp_name */
	sizeof(PluginData),        /* tp_basicsize */
	0,                         /* Will be filled in later */
	0,                         /* tp_dealloc */
	0,                         /* tp_print */
	0,                         /* tp_getattr */
	0,                         /* tp_setattr */
	0,                         /* tp_compare */
	PluginData_repr,           /* tp_repr */
	0,                         /* tp_as_number */
	0,                         /* tp_as_sequence */
	0,                         /* tp_as_mapping */
	0,                         /* tp_hash */
	0,                         /* tp_call */
	0,                         /* tp_str */
	0,                         /* tp_getattro */
	0,                         /* tp_setattro */
	0,                         /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE /*| Py_TPFLAGS_HAVE_GC*/, /*tp_flags*/
	PluginData_doc,            /* tp_doc */
	0,                         /* tp_traverse */
	0,                         /* tp_clear */
	0,                         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	0,                         /* tp_methods */
	PluginData_members,        /* tp_members */
	PluginData_getseters,      /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	0,                         /* tp_dictoffset */
	PluginData_init,           /* tp_init */
	0,                         /* tp_alloc */
	PluginData_new             /* tp_new */
};

static char interval_doc[] = "The interval is the timespan in seconds between two submits for\n"
		"the same data source. This value has to be a positive integer, so you can't\n"
		"submit more than one value per second. If this member is set to a\n"
		"non-positive value, the default value as specified in the config file will\n"
		"be used (default: 10).\n"
		"\n"
		"If you submit values more often than the specified interval, the average\n"
		"will be used. If you submit less values, your graphs will have gaps.";

static char values_doc[] = "These are the actual values that get dispatched to collectd.\n"
		"It has to be a sequence (a tuple or list) of numbers.\n"
		"The size of the sequence and the type of its content depend on the type\n"
		"member your types.db file. For more information on this read the types.db\n"
		"man page.\n"
		"\n"
		"If the sequence does not have the correct size upon dispatch a RuntimeError\n"
		"exception will be raised. If the content of the sequence is not a number,\n"
		"a TypeError exception will be raised.";

static char meta_doc[] = "These are the meta data for this Value object.\n"
		"It has to be a dictionary of numbers, strings or bools. All keys must be\n"
		"strings. int and long objects will be dispatched as signed integers unless\n"
		"they are between 2**63 and 2**64-1, which will result in a unsigned integer.\n"
		"You can force one of these storage classes by using the classes\n"
		"collectd.Signed and collectd.Unsigned. A meta object received by a write\n"
		"callback will always contain Signed or Unsigned objects.";

static char dispatch_doc[] = "dispatch([type][, values][, plugin_instance][, type_instance]"
		"[, plugin][, host][, time][, interval]) -> None.  Dispatch a value list.\n"
		"\n"
		"Dispatch this instance to the collectd process. The object has members\n"
		"for each of the possible arguments for this method. For a detailed explanation\n"
		"of these parameters see the member of the same same.\n"
		"\n"
		"If you do not submit a parameter the value saved in its member will be submitted.\n"
		"If you do provide a parameter it will be used instead, without altering the member.";

static char write_doc[] = "write([destination][, type][, values][, plugin_instance][, type_instance]"
		"[, plugin][, host][, time][, interval]) -> None.  Dispatch a value list.\n"
		"\n"
		"Write this instance to a single plugin or all plugins if 'destination' is obmitted.\n"
		"This will bypass the main collectd process and all filtering and caching.\n"
		"Other than that it works similar to 'dispatch'. In most cases 'dispatch' should be\n"
		"used instead of 'write'.\n";

static char Values_doc[] = "A Values object used for dispatching values to collectd and receiving values from write callbacks.";

static PyObject *Values_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
	Values *self;
	
	self = (Values *) PluginData_new(type, args, kwds);
	if (self == NULL)
		return NULL;
	
	self->values = PyList_New(0);
	self->meta = PyDict_New();
	self->interval = 0;
	return (PyObject *) self;
}

static int Values_init(PyObject *s, PyObject *args, PyObject *kwds) {
	Values *self = (Values *) s;
	double interval = 0, time = 0;
	PyObject *values = NULL, *meta = NULL, *tmp;
	const char *type = "", *plugin_instance = "", *type_instance = "", *plugin = "", *host = "";
	static char *kwlist[] = {"type", "values", "plugin_instance", "type_instance",
			"plugin", "host", "time", "interval", "meta", NULL};
	
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|etOetetetetddO", kwlist,
			NULL, &type, &values, NULL, &plugin_instance, NULL, &type_instance,
			NULL, &plugin, NULL, &host, &time, &interval, &meta))
		return -1;
	
	if (type[0] != 0 && plugin_get_ds(type) == NULL) {
		PyErr_Format(PyExc_TypeError, "Dataset %s not found", type);
		return -1;
	}

	sstrncpy(self->data.host, host, sizeof(self->data.host));
	sstrncpy(self->data.plugin, plugin, sizeof(self->data.plugin));
	sstrncpy(self->data.plugin_instance, plugin_instance, sizeof(self->data.plugin_instance));
	sstrncpy(self->data.type, type, sizeof(self->data.type));
	sstrncpy(self->data.type_instance, type_instance, sizeof(self->data.type_instance));
	self->data.time = time;

	if (values == NULL) {
		values = PyList_New(0);
		PyErr_Clear();
	} else {
		Py_INCREF(values);
	}
	
	if (meta == NULL) {
		meta = PyDict_New();
		PyErr_Clear();
	} else {
		Py_INCREF(meta);
	}
	
	tmp = self->values;
	self->values = values;
	Py_XDECREF(tmp);
	
	tmp = self->meta;
	self->meta = meta;
	Py_XDECREF(tmp);

	self->interval = interval;
	return 0;
}

static meta_data_t *cpy_build_meta(PyObject *meta) {
	int i, s;
	meta_data_t *m = NULL;
	PyObject *l;
	
	if (!meta)
		return NULL;

	l = PyDict_Items(meta); /* New reference. */
	if (!l) {
		cpy_log_exception("building meta data");
		return NULL;
	}
	m = meta_data_create();
	s = PyList_Size(l);
	for (i = 0; i < s; ++i) {
		const char *string, *keystring;
		PyObject *key, *value, *item, *tmp;
		
		item = PyList_GET_ITEM(l, i);
		key = PyTuple_GET_ITEM(item, 0);
		Py_INCREF(key);
		keystring = cpy_unicode_or_bytes_to_string(&key);
		if (!keystring) {
			PyErr_Clear();
			Py_XDECREF(key);
			continue;
		}
		value = PyTuple_GET_ITEM(item, 1);
		Py_INCREF(value);
		if (value == Py_True) {
			meta_data_add_boolean(m, keystring, 1);
		} else if (value == Py_False) {
			meta_data_add_boolean(m, keystring, 0);
		} else if (PyFloat_Check(value)) {
			meta_data_add_double(m, keystring, PyFloat_AsDouble(value));
		} else if (PyObject_TypeCheck(value, &SignedType)) {
			long long int lli;
			lli = PyLong_AsLongLong(value);
			if (!PyErr_Occurred() && (lli == (int64_t) lli))
				meta_data_add_signed_int(m, keystring, lli);
		} else if (PyObject_TypeCheck(value, &UnsignedType)) {
			long long unsigned llu;
			llu = PyLong_AsUnsignedLongLong(value);
			if (!PyErr_Occurred() && (llu == (uint64_t) llu))
				meta_data_add_unsigned_int(m, keystring, llu);
		} else if (PyNumber_Check(value)) {
			long long int lli;
			long long unsigned llu;
			tmp = PyNumber_Long(value);
			lli = PyLong_AsLongLong(tmp);
			if (!PyErr_Occurred() && (lli == (int64_t) lli)) {
				meta_data_add_signed_int(m, keystring, lli);
			} else {
				PyErr_Clear();
				llu = PyLong_AsUnsignedLongLong(tmp);
				if (!PyErr_Occurred() && (llu == (uint64_t) llu))
					meta_data_add_unsigned_int(m, keystring, llu);
			}
			Py_XDECREF(tmp);
		} else {
			string = cpy_unicode_or_bytes_to_string(&value);
			if (string) {
				meta_data_add_string(m, keystring, string);
			} else {
				PyErr_Clear();
				tmp = PyObject_Str(value);
				string = cpy_unicode_or_bytes_to_string(&tmp);
				if (string)
					meta_data_add_string(m, keystring, string);
				Py_XDECREF(tmp);
			}
		}
		if (PyErr_Occurred())
			cpy_log_exception("building meta data");
		Py_XDECREF(value);
		Py_DECREF(key);
	}
	Py_XDECREF(l);
	return m;
}

static PyObject *Values_dispatch(Values *self, PyObject *args, PyObject *kwds) {
	int i, ret;
	const data_set_t *ds;
	int size;
	value_t *value;
	value_list_t value_list = VALUE_LIST_INIT;
	PyObject *values = self->values, *meta = self->meta;
	double time = self->data.time, interval = self->interval;
	const char *host = self->data.host;
	const char *plugin = self->data.plugin;
	const char *plugin_instance = self->data.plugin_instance;
	const char *type = self->data.type;
	const char *type_instance = self->data.type_instance;
	
	static char *kwlist[] = {"type", "values", "plugin_instance", "type_instance",
			"plugin", "host", "time", "interval", "meta", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|etOetetetetddO", kwlist,
			NULL, &type, &values, NULL, &plugin_instance, NULL, &type_instance,
			NULL, &plugin, NULL, &host, &time, &interval, &meta))
		return NULL;

	if (type[0] == 0) {
		PyErr_SetString(PyExc_RuntimeError, "type not set");
		return NULL;
	}
	ds = plugin_get_ds(type);
	if (ds == NULL) {
		PyErr_Format(PyExc_TypeError, "Dataset %s not found", type);
		return NULL;
	}
	if (values == NULL || (PyTuple_Check(values) == 0 && PyList_Check(values) == 0)) {
		PyErr_Format(PyExc_TypeError, "values must be list or tuple");
		return NULL;
	}
	if (meta != NULL && meta != Py_None && !PyDict_Check(meta)) {
		PyErr_Format(PyExc_TypeError, "meta must be a dict");
		return NULL;
	}
	size = (int) PySequence_Length(values);
	if (size != ds->ds_num) {
		PyErr_Format(PyExc_RuntimeError, "type %s needs %d values, got %i", type, ds->ds_num, size);
		return NULL;
	}
	value = malloc(size * sizeof(*value));
	for (i = 0; i < size; ++i) {
		PyObject *item, *num;
		item = PySequence_Fast_GET_ITEM(values, i); /* Borrowed reference. */
		if (ds->ds->type == DS_TYPE_COUNTER) {
			num = PyNumber_Long(item); /* New reference. */
			if (num != NULL) {
				value[i].counter = PyLong_AsUnsignedLongLong(num);
				Py_XDECREF(num);
			}
		} else if (ds->ds->type == DS_TYPE_GAUGE) {
			num = PyNumber_Float(item); /* New reference. */
			if (num != NULL) {
				value[i].gauge = PyFloat_AsDouble(num);
				Py_XDECREF(num);
			}
		} else if (ds->ds->type == DS_TYPE_DERIVE) {
			/* This might overflow without raising an exception.
			 * Not much we can do about it */
			num = PyNumber_Long(item); /* New reference. */
			if (num != NULL) {
				value[i].derive = PyLong_AsLongLong(num);
				Py_XDECREF(num);
			}
		} else if (ds->ds->type == DS_TYPE_ABSOLUTE) {
			/* This might overflow without raising an exception.
			 * Not much we can do about it */
			num = PyNumber_Long(item); /* New reference. */
			if (num != NULL) {
				value[i].absolute = PyLong_AsUnsignedLongLong(num);
				Py_XDECREF(num);
			}
		} else {
			free(value);
			PyErr_Format(PyExc_RuntimeError, "unknown data type %d for %s", ds->ds->type, type);
			return NULL;
		}
		if (PyErr_Occurred() != NULL) {
			free(value);
			return NULL;
		}
	}
	value_list.values = value;
	value_list.meta = cpy_build_meta(meta);
	value_list.values_len = size;
	value_list.time = DOUBLE_TO_CDTIME_T(time);
	value_list.interval = DOUBLE_TO_CDTIME_T(interval);
	sstrncpy(value_list.host, host, sizeof(value_list.host));
	sstrncpy(value_list.plugin, plugin, sizeof(value_list.plugin));
	sstrncpy(value_list.plugin_instance, plugin_instance, sizeof(value_list.plugin_instance));
	sstrncpy(value_list.type, type, sizeof(value_list.type));
	sstrncpy(value_list.type_instance, type_instance, sizeof(value_list.type_instance));
	if (value_list.host[0] == 0)
		sstrncpy(value_list.host, hostname_g, sizeof(value_list.host));
	if (value_list.plugin[0] == 0)
		sstrncpy(value_list.plugin, "python", sizeof(value_list.plugin));
	Py_BEGIN_ALLOW_THREADS;
	ret = plugin_dispatch_values(&value_list);
	Py_END_ALLOW_THREADS;
	meta_data_destroy(value_list.meta);
	free(value);
	if (ret != 0) {
		PyErr_SetString(PyExc_RuntimeError, "error dispatching values, read the logs");
		return NULL;
	}
	Py_RETURN_NONE;
}

static PyObject *Values_write(Values *self, PyObject *args, PyObject *kwds) {
	int i, ret;
	const data_set_t *ds;
	int size;
	value_t *value;
	value_list_t value_list = VALUE_LIST_INIT;
	PyObject *values = self->values, *meta = self->meta;
	double time = self->data.time, interval = self->interval;
	const char *host = self->data.host;
	const char *plugin = self->data.plugin;
	const char *plugin_instance = self->data.plugin_instance;
	const char *type = self->data.type;
	const char *type_instance = self->data.type_instance;
	const char *dest = NULL;
	
	static char *kwlist[] = {"destination", "type", "values", "plugin_instance", "type_instance",
			"plugin", "host", "time", "interval", "meta", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|etOetetetetddO", kwlist,
			NULL, &type, &values, NULL, &plugin_instance, NULL, &type_instance,
			NULL, &plugin, NULL, &host, &time, &interval, &meta))
		return NULL;

	if (type[0] == 0) {
		PyErr_SetString(PyExc_RuntimeError, "type not set");
		return NULL;
	}
	ds = plugin_get_ds(type);
	if (ds == NULL) {
		PyErr_Format(PyExc_TypeError, "Dataset %s not found", type);
		return NULL;
	}
	if (values == NULL || (PyTuple_Check(values) == 0 && PyList_Check(values) == 0)) {
		PyErr_Format(PyExc_TypeError, "values must be list or tuple");
		return NULL;
	}
	size = (int) PySequence_Length(values);
	if (size != ds->ds_num) {
		PyErr_Format(PyExc_RuntimeError, "type %s needs %d values, got %i", type, ds->ds_num, size);
		return NULL;
	}
	value = malloc(size * sizeof(*value));
	for (i = 0; i < size; ++i) {
		PyObject *item, *num;
		item = PySequence_Fast_GET_ITEM(values, i); /* Borrowed reference. */
		if (ds->ds->type == DS_TYPE_COUNTER) {
			num = PyNumber_Long(item); /* New reference. */
			if (num != NULL) {
				value[i].counter = PyLong_AsUnsignedLongLong(num);
				Py_XDECREF(num);
			}
		} else if (ds->ds->type == DS_TYPE_GAUGE) {
			num = PyNumber_Float(item); /* New reference. */
			if (num != NULL) {
				value[i].gauge = PyFloat_AsDouble(num);
				Py_XDECREF(num);
			}
		} else if (ds->ds->type == DS_TYPE_DERIVE) {
			/* This might overflow without raising an exception.
			 * Not much we can do about it */
			num = PyNumber_Long(item); /* New reference. */
			if (num != NULL) {
				value[i].derive = PyLong_AsLongLong(num);
				Py_XDECREF(num);
			}
		} else if (ds->ds->type == DS_TYPE_ABSOLUTE) {
			/* This might overflow without raising an exception.
			 * Not much we can do about it */
			num = PyNumber_Long(item); /* New reference. */
			if (num != NULL) {
				value[i].absolute = PyLong_AsUnsignedLongLong(num);
				Py_XDECREF(num);
			}
		} else {
			free(value);
			PyErr_Format(PyExc_RuntimeError, "unknown data type %d for %s", ds->ds->type, type);
			return NULL;
		}
		if (PyErr_Occurred() != NULL) {
			free(value);
			return NULL;
		}
	}
	value_list.values = value;
	value_list.values_len = size;
	value_list.time = DOUBLE_TO_CDTIME_T(time);
	value_list.interval = DOUBLE_TO_CDTIME_T(interval);
	sstrncpy(value_list.host, host, sizeof(value_list.host));
	sstrncpy(value_list.plugin, plugin, sizeof(value_list.plugin));
	sstrncpy(value_list.plugin_instance, plugin_instance, sizeof(value_list.plugin_instance));
	sstrncpy(value_list.type, type, sizeof(value_list.type));
	sstrncpy(value_list.type_instance, type_instance, sizeof(value_list.type_instance));
	value_list.meta = cpy_build_meta(meta);;
	if (value_list.host[0] == 0)
		sstrncpy(value_list.host, hostname_g, sizeof(value_list.host));
	if (value_list.plugin[0] == 0)
		sstrncpy(value_list.plugin, "python", sizeof(value_list.plugin));
	Py_BEGIN_ALLOW_THREADS;
	ret = plugin_write(dest, NULL, &value_list);
	Py_END_ALLOW_THREADS;
	meta_data_destroy(value_list.meta);
	free(value);
	if (ret != 0) {
		PyErr_SetString(PyExc_RuntimeError, "error dispatching values, read the logs");
		return NULL;
	}
	Py_RETURN_NONE;
}

static PyObject *Values_repr(PyObject *s) {
	PyObject *ret, *tmp;
	static PyObject *l_interval = NULL, *l_values = NULL, *l_meta = NULL, *l_closing = NULL;
	Values *self = (Values *) s;
	
	if (l_interval == NULL)
		l_interval = cpy_string_to_unicode_or_bytes(",interval=");
	if (l_values == NULL)
		l_values = cpy_string_to_unicode_or_bytes(",values=");
	if (l_meta == NULL)
		l_meta = cpy_string_to_unicode_or_bytes(",meta=");
	if (l_closing == NULL)
		l_closing = cpy_string_to_unicode_or_bytes(")");
	
	if (l_interval == NULL || l_values == NULL || l_meta == NULL || l_closing == NULL)
		return NULL;
	
	ret = cpy_common_repr(s);
	if (self->interval != 0) {
		CPY_STRCAT(&ret, l_interval);
		tmp = PyFloat_FromDouble(self->interval);
		CPY_SUBSTITUTE(PyObject_Repr, tmp, tmp);
		CPY_STRCAT_AND_DEL(&ret, tmp);
	}
	if (self->values && (!PyList_Check(self->values) || PySequence_Length(self->values) > 0)) {
		CPY_STRCAT(&ret, l_values);
		tmp = PyObject_Repr(self->values);
		CPY_STRCAT_AND_DEL(&ret, tmp);
	}
	if (self->meta && (!PyDict_Check(self->meta) || PyDict_Size(self->meta) > 0)) {
		CPY_STRCAT(&ret, l_meta);
		tmp = PyObject_Repr(self->meta);
		CPY_STRCAT_AND_DEL(&ret, tmp);
	}
	CPY_STRCAT(&ret, l_closing);
	return ret;
}

static int Values_traverse(PyObject *self, visitproc visit, void *arg) {
	Values *v = (Values *) self;
	Py_VISIT(v->values);
	Py_VISIT(v->meta);
	return 0;
}

static int Values_clear(PyObject *self) {
	Values *v = (Values *) self;
	Py_CLEAR(v->values);
	Py_CLEAR(v->meta);
	return 0;
}

static void Values_dealloc(PyObject *self) {
	Values_clear(self);
	self->ob_type->tp_free(self);
}

static PyMemberDef Values_members[] = {
	{"interval", T_INT, offsetof(Values, interval), 0, interval_doc},
	{"values", T_OBJECT_EX, offsetof(Values, values), 0, values_doc},
	{"meta", T_OBJECT_EX, offsetof(Values, meta), 0, meta_doc},
	{NULL}
};

static PyMethodDef Values_methods[] = {
	{"dispatch", (PyCFunction) Values_dispatch, METH_VARARGS | METH_KEYWORDS, dispatch_doc},
	{"write", (PyCFunction) Values_write, METH_VARARGS | METH_KEYWORDS, write_doc},
	{NULL}
};

PyTypeObject ValuesType = {
	CPY_INIT_TYPE
	"collectd.Values",         /* tp_name */
	sizeof(Values),            /* tp_basicsize */
	0,                         /* Will be filled in later */
	Values_dealloc,            /* tp_dealloc */
	0,                         /* tp_print */
	0,                         /* tp_getattr */
	0,                         /* tp_setattr */
	0,                         /* tp_compare */
	Values_repr,               /* tp_repr */
	0,                         /* tp_as_number */
	0,                         /* tp_as_sequence */
	0,                         /* tp_as_mapping */
	0,                         /* tp_hash */
	0,                         /* tp_call */
	0,                         /* tp_str */
	0,                         /* tp_getattro */
	0,                         /* tp_setattro */
	0,                         /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE | Py_TPFLAGS_HAVE_GC, /*tp_flags*/
	Values_doc,                /* tp_doc */
	Values_traverse,           /* tp_traverse */
	Values_clear,              /* tp_clear */
	0,                         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	Values_methods,            /* tp_methods */
	Values_members,            /* tp_members */
	0,                         /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	0,                         /* tp_dictoffset */
	Values_init,               /* tp_init */
	0,                         /* tp_alloc */
	Values_new                 /* tp_new */
};

static char severity_doc[] = "The severity of this notification. Assign or compare to\n"
		"NOTIF_FAILURE, NOTIF_WARNING or NOTIF_OKAY.";

static char message_doc[] = "Some kind of description what's going on and why this Notification was generated.";

static char Notification_doc[] = "The Notification class is a wrapper around the collectd notification.\n"
		"It can be used to notify other plugins about bad stuff happening. It works\n"
		"similar to Values but has a severity and a message instead of interval\n"
		"and time.\n"
		"Notifications can be dispatched at any time and can be received with register_notification.";

static int Notification_init(PyObject *s, PyObject *args, PyObject *kwds) {
	Notification *self = (Notification *) s;
	int severity = 0;
	double time = 0;
	const char *message = "";
	const char *type = "", *plugin_instance = "", *type_instance = "", *plugin = "", *host = "";
	static char *kwlist[] = {"type", "message", "plugin_instance", "type_instance",
			"plugin", "host", "time", "severity", NULL};
	
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|etetetetetetdi", kwlist,
			NULL, &type, NULL, &message, NULL, &plugin_instance, NULL, &type_instance,
			NULL, &plugin, NULL, &host, &time, &severity))
		return -1;
	
	if (type[0] != 0 && plugin_get_ds(type) == NULL) {
		PyErr_Format(PyExc_TypeError, "Dataset %s not found", type);
		return -1;
	}

	sstrncpy(self->data.host, host, sizeof(self->data.host));
	sstrncpy(self->data.plugin, plugin, sizeof(self->data.plugin));
	sstrncpy(self->data.plugin_instance, plugin_instance, sizeof(self->data.plugin_instance));
	sstrncpy(self->data.type, type, sizeof(self->data.type));
	sstrncpy(self->data.type_instance, type_instance, sizeof(self->data.type_instance));
	self->data.time = time;

	sstrncpy(self->message, message, sizeof(self->message));
	self->severity = severity;
	return 0;
}

static PyObject *Notification_dispatch(Notification *self, PyObject *args, PyObject *kwds) {
	int ret;
	const data_set_t *ds;
	notification_t notification;
	double t = self->data.time;
	int severity = self->severity;
	const char *host = self->data.host;
	const char *plugin = self->data.plugin;
	const char *plugin_instance = self->data.plugin_instance;
	const char *type = self->data.type;
	const char *type_instance = self->data.type_instance;
	const char *message = self->message;
	
	static char *kwlist[] = {"type", "message", "plugin_instance", "type_instance",
			"plugin", "host", "time", "severity", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|etetetetetetdi", kwlist,
			NULL, &type, NULL, &message, NULL, &plugin_instance, NULL, &type_instance,
			NULL, &plugin, NULL, &host, &t, &severity))
		return NULL;

	if (type[0] == 0) {
		PyErr_SetString(PyExc_RuntimeError, "type not set");
		return NULL;
	}
	ds = plugin_get_ds(type);
	if (ds == NULL) {
		PyErr_Format(PyExc_TypeError, "Dataset %s not found", type);
		return NULL;
	}

	notification.time = DOUBLE_TO_CDTIME_T(t);
	notification.severity = severity;
	sstrncpy(notification.message, message, sizeof(notification.message));
	sstrncpy(notification.host, host, sizeof(notification.host));
	sstrncpy(notification.plugin, plugin, sizeof(notification.plugin));
	sstrncpy(notification.plugin_instance, plugin_instance, sizeof(notification.plugin_instance));
	sstrncpy(notification.type, type, sizeof(notification.type));
	sstrncpy(notification.type_instance, type_instance, sizeof(notification.type_instance));
	notification.meta = NULL;
	if (notification.time == 0)
		notification.time = cdtime();
	if (notification.host[0] == 0)
		sstrncpy(notification.host, hostname_g, sizeof(notification.host));
	if (notification.plugin[0] == 0)
		sstrncpy(notification.plugin, "python", sizeof(notification.plugin));
	Py_BEGIN_ALLOW_THREADS;
	ret = plugin_dispatch_notification(&notification);
	Py_END_ALLOW_THREADS;
	if (ret != 0) {
		PyErr_SetString(PyExc_RuntimeError, "error dispatching notification, read the logs");
		return NULL;
	}
	Py_RETURN_NONE;
}

static PyObject *Notification_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
	Notification *self;
	
	self = (Notification *) PluginData_new(type, args, kwds);
	if (self == NULL)
		return NULL;
	
	self->message[0] = 0;
	self->severity = 0;
	return (PyObject *) self;
}

static int Notification_setstring(PyObject *self, PyObject *value, void *data) {
	char *old;
	const char *new;
	
	if (value == NULL) {
		PyErr_SetString(PyExc_TypeError, "Cannot delete this attribute");
		return -1;
	}
	Py_INCREF(value);
	new = cpy_unicode_or_bytes_to_string(&value);
	if (new == NULL) {
		Py_DECREF(value);
		return -1;
	}
	old = ((char *) self) + (intptr_t) data;
	sstrncpy(old, new, NOTIF_MAX_MSG_LEN);
	Py_DECREF(value);
	return 0;
}

static PyObject *Notification_repr(PyObject *s) {
	PyObject *ret, *tmp;
	static PyObject *l_severity = NULL, *l_message = NULL, *l_closing = NULL;
	Notification *self = (Notification *) s;
	
	if (l_severity == NULL)
		l_severity = cpy_string_to_unicode_or_bytes(",severity=");
	if (l_message == NULL)
		l_message = cpy_string_to_unicode_or_bytes(",message=");
	if (l_closing == NULL)
		l_closing = cpy_string_to_unicode_or_bytes(")");
	
	if (l_severity == NULL || l_message == NULL || l_closing == NULL)
		return NULL;
	
	ret = cpy_common_repr(s);
	if (self->severity != 0) {
		CPY_STRCAT(&ret, l_severity);
		tmp = PyInt_FromLong(self->severity);
		CPY_SUBSTITUTE(PyObject_Repr, tmp, tmp);
		CPY_STRCAT_AND_DEL(&ret, tmp);
	}
	if (self->message[0] != 0) {
		CPY_STRCAT(&ret, l_message);
		tmp = cpy_string_to_unicode_or_bytes(self->message);
		CPY_SUBSTITUTE(PyObject_Repr, tmp, tmp);
		CPY_STRCAT_AND_DEL(&ret, tmp);
	}
	CPY_STRCAT(&ret, l_closing);
	return ret;
}

static PyMethodDef Notification_methods[] = {
	{"dispatch", (PyCFunction) Notification_dispatch, METH_VARARGS | METH_KEYWORDS, dispatch_doc},
	{NULL}
};

static PyMemberDef Notification_members[] = {
	{"severity", T_INT, offsetof(Notification, severity), 0, severity_doc},
	{NULL}
};

static PyGetSetDef Notification_getseters[] = {
	{"message", PluginData_getstring, Notification_setstring, message_doc, (void *) offsetof(Notification, message)},
	{NULL}
};

PyTypeObject NotificationType = {
	CPY_INIT_TYPE
	"collectd.Notification",   /* tp_name */
	sizeof(Notification),      /* tp_basicsize */
	0,                         /* Will be filled in later */
	0,                         /* tp_dealloc */
	0,                         /* tp_print */
	0,                         /* tp_getattr */
	0,                         /* tp_setattr */
	0,                         /* tp_compare */
	Notification_repr,         /* tp_repr */
	0,                         /* tp_as_number */
	0,                         /* tp_as_sequence */
	0,                         /* tp_as_mapping */
	0,                         /* tp_hash */
	0,                         /* tp_call */
	0,                         /* tp_str */
	0,                         /* tp_getattro */
	0,                         /* tp_setattro */
	0,                         /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
	Notification_doc,          /* tp_doc */
	0,                         /* tp_traverse */
	0,                         /* tp_clear */
	0,                         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	Notification_methods,      /* tp_methods */
	Notification_members,      /* tp_members */
	Notification_getseters,    /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	0,                         /* tp_dictoffset */
	Notification_init,         /* tp_init */
	0,                         /* tp_alloc */
	Notification_new           /* tp_new */
};

static char Signed_doc[] = "This is a long by another name. Use it in meta data dicts\n"
		"to choose the way it is stored in the meta data.";

PyTypeObject SignedType = {
	CPY_INIT_TYPE
	"collectd.Signed",         /* tp_name */
	sizeof(Signed),            /* tp_basicsize */
	0,                         /* Will be filled in later */
	0,                         /* tp_dealloc */
	0,                         /* tp_print */
	0,                         /* tp_getattr */
	0,                         /* tp_setattr */
	0,                         /* tp_compare */
	0,                         /* tp_repr */
	0,                         /* tp_as_number */
	0,                         /* tp_as_sequence */
	0,                         /* tp_as_mapping */
	0,                         /* tp_hash */
	0,                         /* tp_call */
	0,                         /* tp_str */
	0,                         /* tp_getattro */
	0,                         /* tp_setattro */
	0,                         /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
	Signed_doc                 /* tp_doc */
};

static char Unsigned_doc[] = "This is a long by another name. Use it in meta data dicts\n"
		"to choose the way it is stored in the meta data.";

PyTypeObject UnsignedType = {
	CPY_INIT_TYPE
	"collectd.Unsigned",       /* tp_name */
	sizeof(Unsigned),          /* tp_basicsize */
	0,                         /* Will be filled in later */
	0,                         /* tp_dealloc */
	0,                         /* tp_print */
	0,                         /* tp_getattr */
	0,                         /* tp_setattr */
	0,                         /* tp_compare */
	0,                         /* tp_repr */
	0,                         /* tp_as_number */
	0,                         /* tp_as_sequence */
	0,                         /* tp_as_mapping */
	0,                         /* tp_hash */
	0,                         /* tp_call */
	0,                         /* tp_str */
	0,                         /* tp_getattro */
	0,                         /* tp_setattro */
	0,                         /* tp_as_buffer */
	Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
	Unsigned_doc               /* tp_doc */
};
