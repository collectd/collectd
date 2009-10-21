#include <Python.h>
#include <structmember.h>

#include "collectd.h"
#include "common.h"

#include "cpython.h"

static PyObject *Config_new(PyTypeObject *type, PyObject *args, PyObject *kwds) {
	Config *self;
	
	self = (Config *) type->tp_alloc(type, 0);
	if (self == NULL)
		return NULL;
	
	self->parent = NULL;
	self->key = NULL;
	self->values = NULL;
	self->children = NULL;
	return (PyObject *) self;
}

static int Config_init(PyObject *s, PyObject *args, PyObject *kwds) {
	PyObject *key = NULL, *parent = NULL, *values = NULL, *children = NULL, *tmp;
	Config *self = (Config *) s;
	static char *kwlist[] = {"key", "parent", "values", "children", NULL};
	
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "S|OOO", kwlist,
			&key, &parent, &values, &children))
		return -1;
	
	if (values == NULL) {
		values = PyTuple_New(0);
		PyErr_Clear();
	}
	if (children == NULL) {
		children = PyTuple_New(0);
		PyErr_Clear();
	}
	tmp = self->key;
	Py_INCREF(key);
	self->key = key;
	Py_XDECREF(tmp);
	if (parent != NULL) {
		tmp = self->parent;
		Py_INCREF(parent);
		self->parent = parent;
		Py_XDECREF(tmp);
	}
	if (values != NULL) {
		tmp = self->values;
		Py_INCREF(values);
		self->values = values;
		Py_XDECREF(tmp);
	}
	if (children != NULL) {
		tmp = self->children;
		Py_INCREF(children);
		self->children = children;
		Py_XDECREF(tmp);
	}
	return 0;
}

static PyObject *Config_repr(PyObject *s) {
	Config *self = (Config *) s;
	
	return PyString_FromFormat("<collectd.Config %snode %s>", self->parent == Py_None ? "root " : "", PyString_AsString(PyObject_Str(self->key)));
}

static int Config_traverse(PyObject *self, visitproc visit, void *arg) {
	Config *c = (Config *) self;
	Py_VISIT(c->parent);
	Py_VISIT(c->key);
	Py_VISIT(c->values);
	Py_VISIT(c->children);
	return 0;
}

static int Config_clear(PyObject *self) {
	Config *c = (Config *) self;
	Py_CLEAR(c->parent);
	Py_CLEAR(c->key);
	Py_CLEAR(c->values);
	Py_CLEAR(c->children);
	return 0;
}

static void Config_dealloc(PyObject *self) {
	Config_clear(self);
	self->ob_type->tp_free(self);
}

static PyMemberDef Config_members[] = {
	{"Parent", T_OBJECT, offsetof(Config, parent), 0, "Parent node"},
	{"Key", T_OBJECT_EX, offsetof(Config, key), 0, "Keyword of this node"},
	{"Values", T_OBJECT_EX, offsetof(Config, values), 0, "Values after the key"},
	{"Children", T_OBJECT_EX, offsetof(Config, children), 0, "Childnodes of this node"},
	{NULL}
};

PyTypeObject ConfigType = {
	PyObject_HEAD_INIT(NULL)
	0,                         /* Always 0 */
	"collectd.Config",         /* tp_name */
	sizeof(Config),            /* tp_basicsize */
	0,                         /* Will be filled in later */
	Config_dealloc,            /* tp_dealloc */
	0,                         /* tp_print */
	0,                         /* tp_getattr */
	0,                         /* tp_setattr */
	0,                         /* tp_compare */
	Config_repr,               /* tp_repr */
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
	Config_traverse,           /* tp_traverse */
	Config_clear,              /* tp_clear */
	0,                         /* tp_richcompare */
	0,                         /* tp_weaklistoffset */
	0,                         /* tp_iter */
	0,                         /* tp_iternext */
	0,                         /* tp_methods */
	Config_members,            /* tp_members */
	0,                         /* tp_getset */
	0,                         /* tp_base */
	0,                         /* tp_dict */
	0,                         /* tp_descr_get */
	0,                         /* tp_descr_set */
	0,                         /* tp_dictoffset */
	Config_init,               /* tp_init */
	0,                         /* tp_alloc */
	Config_new                 /* tp_new */
};

