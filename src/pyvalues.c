#include <Python.h>
#include <structmember.h>

#include "collectd.h"
#include "common.h"

#include "cpython.h"

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
	
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|sssssd", kwlist, &type,
			&plugin_instance, &type_instance, &plugin, &host, &time))
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
	PluginData *self = (PluginData *) s;
	
	return PyString_FromFormat("collectd.Values(type='%s%s%s%s%s%s%s%s%s',time=%lu)", self->type,
			*self->type_instance ? "',type_instance='" : "", self->type_instance,
			*self->plugin ? "',plugin='" : "", self->plugin,
			*self->plugin_instance ? "',plugin_instance='" : "", self->plugin_instance,
			*self->host ? "',host='" : "", self->host,
			(long unsigned) self->time);
}

static PyMemberDef PluginData_members[] = {
	{"time", T_DOUBLE, offsetof(PluginData, time), 0, time_doc},
	{NULL}
};

static PyObject *PluginData_getstring(PyObject *self, void *data) {
	const char *value = ((char *) self) + (intptr_t) data;
	
	return PyString_FromString(value);
}

static int PluginData_setstring(PyObject *self, PyObject *value, void *data) {
	char *old;
	const char *new;
	
	if (value == NULL) {
		PyErr_SetString(PyExc_TypeError, "Cannot delete this attribute");
		return -1;
	}
	new = PyString_AsString(value);
	if (new == NULL) return -1;
	old = ((char *) self) + (intptr_t) data;
	sstrncpy(old, new, DATA_MAX_NAME_LEN);
	return 0;
}

static int PluginData_settype(PyObject *self, PyObject *value, void *data) {
	char *old;
	const char *new;
	
	if (value == NULL) {
		PyErr_SetString(PyExc_TypeError, "Cannot delete this attribute");
		return -1;
	}
	new = PyString_AsString(value);
	if (new == NULL) return -1;

	if (plugin_get_ds(new) == NULL) {
		PyErr_Format(PyExc_TypeError, "Dataset %s not found", new);
		return -1;
	}

	old = ((char *) self) + (intptr_t) data;
	sstrncpy(old, new, DATA_MAX_NAME_LEN);
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
	PyObject_HEAD_INIT(NULL)
	0,                         /* Always 0 */
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
	self->interval = 0;
	return (PyObject *) self;
}

static int Values_init(PyObject *s, PyObject *args, PyObject *kwds) {
	Values *self = (Values *) s;
	int interval = 0, ret;
	double time = 0;
	PyObject *values = NULL, *tmp;
	const char *type = "", *plugin_instance = "", *type_instance = "", *plugin = "", *host = "";
	static char *kwlist[] = {"type", "values", "plugin_instance", "type_instance",
			"plugin", "host", "time", "interval", NULL};
	
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|sOssssdi", kwlist,
			&type, &values, &plugin_instance, &type_instance,
			&plugin, &host, &time, &interval))
		return -1;
	
	tmp = Py_BuildValue("sssssd", type, plugin_instance, type_instance, plugin, host, time);
	if (tmp == NULL)
		return -1;
	ret = PluginDataType.tp_init(s, tmp, NULL);
	Py_DECREF(tmp);
	if (ret != 0)
		return -1;
	
	if (values == NULL) {
		values = PyList_New(0);
		PyErr_Clear();
	} else {
		Py_INCREF(values);
	}
	
	tmp = self->values;
	self->values = values;
	Py_XDECREF(tmp);
	
	self->interval = interval;
	return 0;
}

static PyObject *Values_dispatch(Values *self, PyObject *args, PyObject *kwds) {
	int i, ret;
	const data_set_t *ds;
	Py_ssize_t size;
	value_t *value;
	value_list_t value_list = VALUE_LIST_INIT;
	PyObject *values = self->values;
	double time = self->data.time;
	int interval = self->interval;
	const char *host = self->data.host;
	const char *plugin = self->data.plugin;
	const char *plugin_instance = self->data.plugin_instance;
	const char *type = self->data.type;
	const char *type_instance = self->data.type_instance;
	
	static char *kwlist[] = {"type", "values", "plugin_instance", "type_instance",
			"plugin", "host", "time", "interval", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|sOssssdi", kwlist,
			&type, &values, &plugin_instance, &type_instance,
			&plugin, &host, &time, &interval))
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
	size = PySequence_Length(values);
	if (size != ds->ds_num) {
		PyErr_Format(PyExc_RuntimeError, "type %s needs %d values, got %zd", type, ds->ds_num, size);
		return NULL;
	}
	value = malloc(size * sizeof(*value));
	for (i = 0; i < size; ++i) {
		PyObject *item, *num;
		item = PySequence_GetItem(values, i);
		if (ds->ds->type == DS_TYPE_COUNTER) {
			num = PyNumber_Long(item);
			if (num != NULL)
				value[i].counter = PyLong_AsUnsignedLongLong(num);
		} else if (ds->ds->type == DS_TYPE_GAUGE) {
			num = PyNumber_Float(item);
			if (num != NULL)
				value[i].gauge = PyFloat_AsDouble(num);
		} else if (ds->ds->type == DS_TYPE_DERIVE) {
			/* This might overflow without raising an exception.
			 * Not much we can do about it */
			num = PyNumber_Long(item);
			if (num != NULL)
				value[i].derive = PyLong_AsLongLong(num);
		} else if (ds->ds->type == DS_TYPE_ABSOLUTE) {
			/* This might overflow without raising an exception.
			 * Not much we can do about it */
			num = PyNumber_Long(item);
			if (num != NULL)
				value[i].absolute = PyLong_AsUnsignedLongLong(num);
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
	value_list.time = time;
	value_list.interval = interval;
	sstrncpy(value_list.host, host, sizeof(value_list.host));
	sstrncpy(value_list.plugin, plugin, sizeof(value_list.plugin));
	sstrncpy(value_list.plugin_instance, plugin_instance, sizeof(value_list.plugin_instance));
	sstrncpy(value_list.type, type, sizeof(value_list.type));
	sstrncpy(value_list.type_instance, type_instance, sizeof(value_list.type_instance));
	value_list.meta = NULL;
	if (value_list.host[0] == 0)
		sstrncpy(value_list.host, hostname_g, sizeof(value_list.host));
	if (value_list.plugin[0] == 0)
		sstrncpy(value_list.plugin, "python", sizeof(value_list.plugin));
	Py_BEGIN_ALLOW_THREADS;
	ret = plugin_dispatch_values(&value_list);
	Py_END_ALLOW_THREADS;
	if (ret != 0) {
		PyErr_SetString(PyExc_RuntimeError, "error dispatching values, read the logs");
		return NULL;
	}
	free(value);
	Py_RETURN_NONE;
}

static PyObject *Values_write(Values *self, PyObject *args, PyObject *kwds) {
	int i, ret;
	const data_set_t *ds;
	Py_ssize_t size;
	value_t *value;
	value_list_t value_list = VALUE_LIST_INIT;
	PyObject *values = self->values;
	double time = self->data.time;
	int interval = self->interval;
	const char *host = self->data.host;
	const char *plugin = self->data.plugin;
	const char *plugin_instance = self->data.plugin_instance;
	const char *type = self->data.type;
	const char *type_instance = self->data.type_instance;
	const char *dest = NULL;
	
	static char *kwlist[] = {"destination", "type", "values", "plugin_instance", "type_instance",
			"plugin", "host", "time", "interval", NULL};
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|sOssssdi", kwlist,
			&type, &values, &plugin_instance, &type_instance,
			&plugin, &host, &time, &interval))
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
	size = PySequence_Length(values);
	if (size != ds->ds_num) {
		PyErr_Format(PyExc_RuntimeError, "type %s needs %d values, got %zd", type, ds->ds_num, size);
		return NULL;
	}
	value = malloc(size * sizeof(*value));
	for (i = 0; i < size; ++i) {
		PyObject *item, *num;
		item = PySequence_GetItem(values, i);
		if (ds->ds->type == DS_TYPE_COUNTER) {
			num = PyNumber_Long(item);
			if (num != NULL)
				value[i].counter = PyLong_AsUnsignedLongLong(num);
		} else if (ds->ds->type == DS_TYPE_GAUGE) {
			num = PyNumber_Float(item);
			if (num != NULL)
				value[i].gauge = PyFloat_AsDouble(num);
		} else if (ds->ds->type == DS_TYPE_DERIVE) {
			/* This might overflow without raising an exception.
			 * Not much we can do about it */
			num = PyNumber_Long(item);
			if (num != NULL)
				value[i].derive = PyLong_AsLongLong(num);
		} else if (ds->ds->type == DS_TYPE_ABSOLUTE) {
			/* This might overflow without raising an exception.
			 * Not much we can do about it */
			num = PyNumber_Long(item);
			if (num != NULL)
				value[i].absolute = PyLong_AsUnsignedLongLong(num);
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
	value_list.time = time;
	value_list.interval = interval;
	sstrncpy(value_list.host, host, sizeof(value_list.host));
	sstrncpy(value_list.plugin, plugin, sizeof(value_list.plugin));
	sstrncpy(value_list.plugin_instance, plugin_instance, sizeof(value_list.plugin_instance));
	sstrncpy(value_list.type, type, sizeof(value_list.type));
	sstrncpy(value_list.type_instance, type_instance, sizeof(value_list.type_instance));
	value_list.meta = NULL;
	if (value_list.host[0] == 0)
		sstrncpy(value_list.host, hostname_g, sizeof(value_list.host));
	if (value_list.plugin[0] == 0)
		sstrncpy(value_list.plugin, "python", sizeof(value_list.plugin));
	Py_BEGIN_ALLOW_THREADS;
	ret = plugin_write(dest, NULL, &value_list);
	Py_END_ALLOW_THREADS;
	if (ret != 0) {
		PyErr_SetString(PyExc_RuntimeError, "error dispatching values, read the logs");
		return NULL;
	}
	free(value);
	Py_RETURN_NONE;
}

static PyObject *Values_repr(PyObject *s) {
	PyObject *ret, *valuestring = NULL;
	Values *self = (Values *) s;
	
	if (self->values != NULL)
		valuestring = PyObject_Repr(self->values);
	if (valuestring == NULL)
		return NULL;
	
	ret = PyString_FromFormat("collectd.Values(type='%s%s%s%s%s%s%s%s%s',time=%lu,interval=%i,values=%s)", self->data.type,
			*self->data.type_instance ? "',type_instance='" : "", self->data.type_instance,
			*self->data.plugin ? "',plugin='" : "", self->data.plugin,
			*self->data.plugin_instance ? "',plugin_instance='" : "", self->data.plugin_instance,
			*self->data.host ? "',host='" : "", self->data.host,
			(long unsigned) self->data.time, self->interval,
			valuestring ? PyString_AsString(valuestring) : "[]");
	Py_XDECREF(valuestring);
	return ret;
}

static int Values_traverse(PyObject *self, visitproc visit, void *arg) {
	Values *v = (Values *) self;
	Py_VISIT(v->values);
	return 0;
}

static int Values_clear(PyObject *self) {
	Values *v = (Values *) self;
	Py_CLEAR(v->values);
	return 0;
}

static void Values_dealloc(PyObject *self) {
	Values_clear(self);
	self->ob_type->tp_free(self);
}

static PyMemberDef Values_members[] = {
	{"interval", T_INT, offsetof(Values, interval), 0, interval_doc},
	{"values", T_OBJECT_EX, offsetof(Values, values), 0, values_doc},
	{NULL}
};

static PyMethodDef Values_methods[] = {
	{"dispatch", (PyCFunction) Values_dispatch, METH_VARARGS | METH_KEYWORDS, dispatch_doc},
	{"write", (PyCFunction) Values_write, METH_VARARGS | METH_KEYWORDS, write_doc},
	{NULL}
};

PyTypeObject ValuesType = {
	PyObject_HEAD_INIT(NULL)
	0,                         /* Always 0 */
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
	PyObject *tmp;
	int severity = 0, ret;
	double time = 0;
	const char *message = "";
	const char *type = "", *plugin_instance = "", *type_instance = "", *plugin = "", *host = "";
	static char *kwlist[] = {"type", "message", "plugin_instance", "type_instance",
			"plugin", "host", "time", "severity", NULL};
	
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ssssssdi", kwlist,
			&type, &message, &plugin_instance, &type_instance,
			&plugin, &host, &time, &severity))
		return -1;
	
	tmp = Py_BuildValue("sssssd", type, plugin_instance, type_instance, plugin, host, time);
	if (tmp == NULL)
		return -1;
	ret = PluginDataType.tp_init(s, tmp, NULL);
	Py_DECREF(tmp);
	if (ret != 0)
		return -1;
	
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
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|ssssssdi", kwlist,
			&type, &message, &plugin_instance, &type_instance,
			&plugin, &host, &t, &severity))
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

	notification.time = t;
	notification.severity = severity;
	sstrncpy(notification.message, message, sizeof(notification.message));
	sstrncpy(notification.host, host, sizeof(notification.host));
	sstrncpy(notification.plugin, plugin, sizeof(notification.plugin));
	sstrncpy(notification.plugin_instance, plugin_instance, sizeof(notification.plugin_instance));
	sstrncpy(notification.type, type, sizeof(notification.type));
	sstrncpy(notification.type_instance, type_instance, sizeof(notification.type_instance));
	notification.meta = NULL;
	if (notification.time < 1)
		notification.time = time(0);
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
	new = PyString_AsString(value);
	if (new == NULL) return -1;
	old = ((char *) self) + (intptr_t) data;
	sstrncpy(old, new, NOTIF_MAX_MSG_LEN);
	return 0;
}

static PyObject *Notification_repr(PyObject *s) {
	PyObject *ret;
	Notification *self = (Notification *) s;
	
	ret = PyString_FromFormat("collectd.Values(type='%s%s%s%s%s%s%s%s%s%s%s',time=%lu,interval=%i)", self->data.type,
			*self->data.type_instance ? "',type_instance='" : "", self->data.type_instance,
			*self->data.plugin ? "',plugin='" : "", self->data.plugin,
			*self->data.plugin_instance ? "',plugin_instance='" : "", self->data.plugin_instance,
			*self->data.host ? "',host='" : "", self->data.host,
			*self->message ? "',message='" : "", self->message,
			(long unsigned) self->data.time, self->severity);
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
	PyObject_HEAD_INIT(NULL)
	0,                         /* Always 0 */
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
