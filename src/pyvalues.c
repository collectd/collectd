#include <Python.h>
#include <structmember.h>

#include "collectd.h"
#include "common.h"

#include "cpython.h"

static PyObject *Values_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
	Values *self;
	
	self = (Values *) type->tp_alloc(type, 0);
	if (self == NULL)
		return NULL;
	
	self->values = PyList_New(0);
	self->time = 0;
	self->interval = 0;
	self->host[0] = 0;
	self->plugin[0] = 0;
	self->plugin_instance[0] = 0;
	self->type[0] = 0;
	self->type_instance[0] = 0;
	return (PyObject *) self;
}

static int Values_init(PyObject *s, PyObject *args, PyObject *kwds) {
	Values *self = (Values *) s;
	int interval = 0;
	double time = 0;
	PyObject *values = NULL, *tmp;
	const char *type = "", *plugin_instance = "", *type_instance = "", *plugin = "", *host = "";
	static char *kwlist[] = {"type", "values", "plugin_instance", "type_instance",
			"plugin", "host", "time", "interval", NULL};
	
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "|sOssssdi", kwlist,
			&type, &values, &plugin_instance, &type_instance,
			&plugin, &host, &time, &interval))
		return -1;
	
	if (type[0] != 0 && plugin_get_ds(type) == NULL) {
		PyErr_Format(PyExc_TypeError, "Dataset %s not found", type);
		return -1;
	}

	if (values == NULL) {
		values = PyList_New(0);
		PyErr_Clear();
	} else {
		Py_INCREF(values);
	}
	
	tmp = self->values;
	self->values = values;
	Py_XDECREF(tmp);
	
	sstrncpy(self->host, host, sizeof(self->host));
	sstrncpy(self->plugin, plugin, sizeof(self->plugin));
	sstrncpy(self->plugin_instance, plugin_instance, sizeof(self->plugin_instance));
	sstrncpy(self->type, type, sizeof(self->type));
	sstrncpy(self->type_instance, type_instance, sizeof(self->type_instance));
	
	self->time = time;
	self->interval = interval;
	return 0;
}

static PyObject *Values_repr(PyObject *s) {
	PyObject *ret, *valuestring = NULL;
	Values *self = (Values *) s;
	
	if (self->values != NULL)
		valuestring = PyObject_Repr(self->values);
	if (valuestring == NULL)
		return NULL;
	
	ret = PyString_FromFormat("collectd.Values(type='%s%s%s%s%s%s%s%s%s',time=%lu,interval=%i,values=%s)", self->type,
			*self->type_instance ? "',type_instance='" : "", self->type_instance,
			*self->plugin ? "',plugin='" : "", self->plugin,
			*self->plugin_instance ? "',plugin_instance='" : "", self->plugin_instance,
			*self->host ? "',host='" : "", self->host,
			(long unsigned) self->time, self->interval,
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
	{"time", T_DOUBLE, offsetof(Values, time), 0, "Parent node"},
	{"interval", T_INT, offsetof(Values, interval), 0, "Keyword of this node"},
	{"values", T_OBJECT_EX, offsetof(Values, values), 0, "Values after the key"},
//	{"Children", T_OBJECT_EX, offsetof(Config, children), 0, "Childnodes of this node"},
	{NULL}
};

static PyObject *Values_getstring(PyObject *self, void *data) {
	const char *value = ((char *) self) + (int) data;
	
	return PyString_FromString(value);
}

static int Values_setstring(PyObject *self, PyObject *value, void *data) {
	char *old;
	const char *new;
	
	if (value == NULL) {
		PyErr_SetString(PyExc_TypeError, "Cannot delete this attribute");
		return -1;
	}
	new = PyString_AsString(value);
	if (new == NULL) return -1;
	old = ((char *) self) + (int) data;
	sstrncpy(old, new, DATA_MAX_NAME_LEN);
	return 0;
}

static int Values_settype(PyObject *self, PyObject *value, void *data) {
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

	old = ((char *) self) + (int) data;
	sstrncpy(old, new, DATA_MAX_NAME_LEN);
	return 0;
}

static PyObject *Values_dispatch(Values *self, PyObject *args, PyObject *kwds) {
	int i, ret;
	const data_set_t *ds;
	Py_ssize_t size;
	value_t *value;
	value_list_t value_list = VALUE_LIST_INIT;
	PyObject *values = self->values;
	double time = self->time;
	int interval = self->interval;
	const char *host = self->host;
	const char *plugin = self->plugin;
	const char *plugin_instance = self->plugin_instance;
	const char *type = self->type;
	const char *type_instance = self->type_instance;
	
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
				value[i].gauge = PyLong_AsLongLong(num);
		} else if (ds->ds->type == DS_TYPE_ABSOLUTE) {
			/* This might overflow without raising an exception.
			 * Not much we can do about it */
			num = PyNumber_Long(item);
			if (num != NULL)
				value[i].gauge = PyLong_AsUnsignedLongLong(num);
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

static PyGetSetDef Values_getseters[] = {
	{"host", Values_getstring, Values_setstring, "help text", (void *) offsetof(Values, host)},
	{"plugin", Values_getstring, Values_setstring, "help text", (void *) offsetof(Values, plugin)},
	{"plugin_instance", Values_getstring, Values_setstring, "help text", (void *) offsetof(Values, plugin_instance)},
	{"type_instance", Values_getstring, Values_setstring, "help text", (void *) offsetof(Values, type_instance)},
	{"type", Values_getstring, Values_settype, "help text", (void *) offsetof(Values, type)},
	{NULL}
};

static PyMethodDef Values_methods[] = {
	{"dispatch", (PyCFunction) Values_dispatch, METH_VARARGS | METH_KEYWORDS, "help text"},
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
	"Cool help text later",    /* tp_doc */
	Values_traverse,           /* tp_traverse */
	Values_clear,              /* tp_clear */
	0,                         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	Values_methods,            /* tp_methods */
	Values_members,            /* tp_members */
	Values_getseters,          /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	0,                         /* tp_dictoffset */
	Values_init,               /* tp_init */
	0,                         /* tp_alloc */
	Values_new                 /* tp_new */
};

