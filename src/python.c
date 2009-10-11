#include <Python.h>
#include <structmember.h>

#include "collectd.h"
#include "common.h"

typedef struct cpy_callback_s {
	char *name;
	PyObject *callback;
	struct cpy_callback_s *next;
} cpy_callback_t;

/* This is our global thread state. Python saves some stuff in thread-local
 * storage. So if we allow the interpreter to run in the background
 * (the scriptwriters might have created some threads from python), we have
 * to save the state so we can resume it later from a different thread.

 * Technically the Global Interpreter Lock (GIL) and thread states can be
 * manipulated independently. But to keep stuff from getting too complex
 * we'll just use PyEval_SaveTread and PyEval_RestoreThreas which takes
 * care of the thread states as well as the GIL. */

static PyThreadState *state;

static cpy_callback_t *cpy_config_callbacks;

typedef struct {
	PyObject_HEAD      /* No semicolon! */
	PyObject *parent;
	PyObject *key;
	PyObject *values;
	PyObject *children;
} Config;

static void Config_dealloc(PyObject *s) {
	Config *self = (Config *) s;
	
	Py_XDECREF(self->parent);
	Py_XDECREF(self->key);
	Py_XDECREF(self->values);
	Py_XDECREF(self->children);
	self->ob_type->tp_free(s);
}

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

static PyMemberDef Config_members[] = {
    {"Parent", T_OBJECT, offsetof(Config, parent), 0, "Parent node"},
    {"Key", T_OBJECT_EX, offsetof(Config, key), 0, "Keyword of this node"},
    {"Values", T_OBJECT_EX, offsetof(Config, values), 0, "Values after the key"},
    {"Children", T_OBJECT_EX, offsetof(Config, children), 0, "Childnodes of this node"},
    {NULL}
};

static PyTypeObject ConfigType = {
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
    "Cool help text later",    /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
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

static PyObject *cpy_register_config(PyObject *self, PyObject *args) {
	cpy_callback_t *c;
	const char *name = NULL;
	PyObject *callback = NULL;
	
	if (PyArg_ParseTuple(args, "O|z", &callback, &name) == 0) return NULL;
	if (PyCallable_Check(callback) == 0) {
		PyErr_SetString(PyExc_TypeError, "callback needs a be a callable object.");
		return 0;
	}
	if (name == NULL) {
		PyObject *mod;
		
		mod = PyObject_GetAttrString(callback, "__module__");
		if (mod != NULL) name = PyString_AsString(mod);
		if (name == NULL) {
			PyErr_SetString(PyExc_ValueError, "No module name specified and "
				"callback function does not have a \"__module__\" attribute.");
			return 0;
		}
	}
	c = malloc(sizeof(*c));
	c->name = strdup(name);
	c->callback = callback;
	c->next = cpy_config_callbacks;
	cpy_config_callbacks = c;
	return Py_None;
}

static PyMethodDef cpy_methods[] = {
	{"register_config", cpy_register_config, METH_VARARGS, "foo"},
	{0, 0, 0, 0}
};

static int cpy_shutdown(void) {
	/* This can happen if the module was loaded but not configured. */
	if (state != NULL)
		PyEval_RestoreThread(state);
	Py_Finalize();
	return 0;
}

static int cpy_init(void) {
	PyEval_InitThreads();
	/* Now it's finally OK to use python threads. */
	return 0;
}

static PyObject *cpy_oconfig_to_pyconfig(oconfig_item_t *ci, PyObject *parent) {
	int i;
	PyObject *item, *values, *children, *tmp;
	
	if (parent == NULL)
		parent = Py_None;
	
	values = PyTuple_New(ci->values_num); /* New reference. */
	for (i = 0; i < ci->values_num; ++i) {
		if (ci->values[i].type == OCONFIG_TYPE_STRING) {
			PyTuple_SET_ITEM(values, i, PyString_FromString(ci->values[i].value.string));
		} else if (ci->values[i].type == OCONFIG_TYPE_NUMBER) {
			PyTuple_SET_ITEM(values, i, PyFloat_FromDouble(ci->values[i].value.number));
		} else if (ci->values[i].type == OCONFIG_TYPE_BOOLEAN) {
			PyTuple_SET_ITEM(values, i, PyBool_FromLong(ci->values[i].value.boolean));
		}
	}
	
	item = PyObject_CallFunction((PyObject *) &ConfigType, "sONO", ci->key, parent, values, Py_None);
	if (item == NULL)
		return NULL;
	children = PyTuple_New(ci->children_num); /* New reference. */
	for (i = 0; i < ci->children_num; ++i) {
			PyTuple_SET_ITEM(children, i, cpy_oconfig_to_pyconfig(ci->children + i, item));
	}
	tmp = ((Config *) item)->children;
	((Config *) item)->children = children;
	Py_XDECREF(tmp);
	return item;
}

static int cpy_config(oconfig_item_t *ci) {
	int i;
	PyObject *sys;
	PyObject *sys_path;
	PyObject *module;
	
	/* Ok in theory we shouldn't do initialization at this point
	 * but we have to. In order to give python scripts a chance
	 * to register a config callback we need to be able to execute
	 * python code during the config callback so we have to start
	 * the interpreter here. */
	/* Do *not* use the python "thread" module at this point! */
	Py_Initialize();
	
	PyType_Ready(&ConfigType);
	sys = PyImport_ImportModule("sys"); /* New reference. */
	if (sys == NULL) {
		ERROR("python module: Unable to import \"sys\" module.");
		/* Just print the default python exception text to stderr. */
		PyErr_Print();
		return 1;
	}
	sys_path = PyObject_GetAttrString(sys, "path"); /* New reference. */
	Py_DECREF(sys);
	if (sys_path == NULL) {
		ERROR("python module: Unable to read \"sys.path\".");
		PyErr_Print();
		return 1;
	}
	module = Py_InitModule("collectd", cpy_methods); /* Borrowed reference. */
	PyModule_AddObject(module, "Config", (PyObject *) &ConfigType); /* Steals a reference. */
	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *item = ci->children + i;
		
		if (strcasecmp(item->key, "ModulePath") == 0) {
			char *dir = NULL;
			PyObject *dir_object;
			
			if (cf_util_get_string(item, &dir) != 0) 
				continue;
			dir_object = PyString_FromString(dir); /* New reference. */
			if (dir_object == NULL) {
				ERROR("python plugin: Unable to convert \"%s\" to "
				      "a python object.", dir);
				free(dir);
				PyErr_Print();
				continue;
			}
			if (PyList_Append(sys_path, dir_object) != 0) {
				ERROR("python plugin: Unable to append \"%s\" to "
				      "python module path.", dir);
				PyErr_Print();
			}
			Py_DECREF(dir_object);
			free(dir);
		} else if (strcasecmp(item->key, "Import") == 0) {
			char *module_name = NULL;
			PyObject *module;
			
			if (cf_util_get_string(item, &module_name) != 0) 
				continue;
			module = PyImport_ImportModule(module_name); /* New reference. */
			if (module == NULL) {
				ERROR("python plugin: Error importing module \"%s\".", module_name);
				PyErr_Print();
			}
			free(module_name);
			Py_XDECREF(module);
		} else if (strcasecmp(item->key, "Module") == 0) {
			char *name = NULL;
			cpy_callback_t *c;
			PyObject *ret;
			
			if (cf_util_get_string(item, &name) != 0)
				continue;
			for (c = cpy_config_callbacks; c; c = c->next) {
				if (strcasecmp(c->name, name) == 0)
					break;
			}
			if (c == NULL) {
				WARNING("python plugin: Found a configuration for the \"%s\" plugin, "
					"but the plugin isn't loaded or didn't register "
					"a configuration callback.", name);
				free(name);
				continue;
			}
			free(name);
			ret = PyObject_CallFunction(c->callback, "N",
					cpy_oconfig_to_pyconfig(item, NULL)); /* New reference. */
			if (ret == NULL)
				PyErr_Print();
			else
				Py_DECREF(ret);
		} else {
			WARNING("python plugin: Ignoring unknown config key \"%s\".", item->key);
		}
	}
	Py_DECREF(sys_path);
	return 0;
}

void module_register(void) {
	plugin_register_complex_config("python", cpy_config);
	plugin_register_init("python", cpy_init);
//	plugin_register_read("netapp", cna_read);
	plugin_register_shutdown("netapp", cpy_shutdown);
}
