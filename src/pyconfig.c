/**
 * collectd - src/pyconfig.c
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

static char config_doc[] = "This represents a piece of collectd's config file.\n"
		"It is passed to scripts with config callbacks (see \"register_config\")\n"
		"and is of little use if created somewhere else.\n"
		"\n"
		"It has no methods beyond the bare minimum and only exists for its\n"
		"data members";

static char parent_doc[] = "This represents the parent of this node. On the root node\n"
		"of the config tree it will be None.\n";

static char key_doc[] = "This is the keyword of this item, ie the first word of any\n"
		"given line in the config file. It will always be a string.\n";

static char values_doc[] = "This is a tuple (which might be empty) of all value, ie words\n"
		"following the keyword in any given line in the config file.\n"
		"\n"
		"Every item in this tuple will be either a string or a float or a bool,\n"
		"depending on the contents of the configuration file.\n";

static char children_doc[] = "This is a tuple of child nodes. For most nodes this will be\n"
		"empty. If this node represents a block instead of a single line of the config\n"
		"file it will contain all nodes in this block.\n";

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
	
	if (!PyArg_ParseTupleAndKeywords(args, kwds, "O|OOO", kwlist,
			&key, &parent, &values, &children))
		return -1;
	
	if (!IS_BYTES_OR_UNICODE(key)) {
		PyErr_SetString(PyExc_TypeError, "argument 1 must be str");
		Py_XDECREF(parent);
		Py_XDECREF(values);
		Py_XDECREF(children);
		return -1;
	}
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
	PyObject *ret = NULL;
	static PyObject *node_prefix = NULL, *root_prefix = NULL, *ending = NULL;
	
	/* This is ok because we have the GIL, so this is thread-save by default. */
	if (node_prefix == NULL)
		node_prefix = cpy_string_to_unicode_or_bytes("<collectd.Config node ");
	if (root_prefix == NULL)
		root_prefix = cpy_string_to_unicode_or_bytes("<collectd.Config root node ");
	if (ending == NULL)
		ending = cpy_string_to_unicode_or_bytes(">");
	if (node_prefix == NULL || root_prefix == NULL || ending == NULL)
		return NULL;
	
	ret = PyObject_Str(self->key);
	CPY_SUBSTITUTE(PyObject_Repr, ret, ret);
	if (self->parent == NULL || self->parent == Py_None)
		CPY_STRCAT(&ret, root_prefix);
	else
		CPY_STRCAT(&ret, node_prefix);
	CPY_STRCAT(&ret, ending);
	
	return ret;
}

static int Config_traverse(PyObject *self, visitproc visit, void *arg) {
	Config *c = (Config *) self;
	Py_VISIT(c->parent);
	Py_VISIT(c->key);
	Py_VISIT(c->values);
	Py_VISIT(c->children);
	return 0;}

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
	{"parent", T_OBJECT, offsetof(Config, parent), 0, parent_doc},
	{"key", T_OBJECT_EX, offsetof(Config, key), 0, key_doc},
	{"values", T_OBJECT_EX, offsetof(Config, values), 0, values_doc},
	{"children", T_OBJECT_EX, offsetof(Config, children), 0, children_doc},
	{NULL}
};

PyTypeObject ConfigType = {
	CPY_INIT_TYPE
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
	config_doc,                /* tp_doc */
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

