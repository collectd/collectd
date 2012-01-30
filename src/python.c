/**
 * collectd - src/python.c
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

#include <signal.h>
#if HAVE_PTHREAD_H
# include <pthread.h>
#endif

#include "collectd.h"
#include "common.h"

#include "cpython.h"

typedef struct cpy_callback_s {
	char *name;
	PyObject *callback;
	PyObject *data;
	struct cpy_callback_s *next;
} cpy_callback_t;

static char log_doc[] = "This function sends a string to all logging plugins.";

static char flush_doc[] = "flush([plugin][, timeout][, identifier]) -> None\n"
		"\n"
		"Flushes the cache of another plugin.";

static char unregister_doc[] = "Unregisters a callback. This function needs exactly one parameter either\n"
		"the function to unregister or the callback identifier to unregister.";

static char reg_log_doc[] = "register_log(callback[, data][, name]) -> identifier\n"
		"\n"
		"Register a callback function for log messages.\n"
		"\n"
		"'callback' is a callable object that will be called every time something\n"
		"    is logged.\n"
		"'data' is an optional object that will be passed back to the callback\n"
		"    function every time it is called.\n"
		"'name' is an optional identifier for this callback. The default name\n"
		"    is 'python.<module>'.\n"
		"    Every callback needs a unique identifier, so if you want to\n"
		"    register this callback multiple time from the same module you need\n"
		"    to specify a name here.\n"
		"'identifier' is the full identifier assigned to this callback.\n"
		"\n"
		"The callback function will be called with two or three parameters:\n"
		"severity: An integer that should be compared to the LOG_ constants.\n"
		"message: The text to be logged.\n"
		"data: The optional data parameter passed to the register function.\n"
		"    If the parameter was omitted it will be omitted here, too.";

static char reg_init_doc[] = "register_init(callback[, data][, name]) -> identifier\n"
		"\n"
		"Register a callback function that will be executed once after the config.\n"
		"file has been read, all plugins heve been loaded and the collectd has\n"
		"forked into the background.\n"
		"\n"
		"'callback' is a callable object that will be executed.\n"
		"'data' is an optional object that will be passed back to the callback\n"
		"    function when it is called.\n"
		"'name' is an optional identifier for this callback. The default name\n"
		"    is 'python.<module>'.\n"
		"    Every callback needs a unique identifier, so if you want to\n"
		"    register this callback multiple time from the same module you need\n"
		"    to specify a name here.\n"
		"'identifier' is the full identifier assigned to this callback.\n"
		"\n"
		"The callback function will be called without parameters, except for\n"
		"data if it was supplied.";

static char reg_config_doc[] = "register_config(callback[, data][, name]) -> identifier\n"
		"\n"
		"Register a callback function for config file entries.\n"
		"'callback' is a callable object that will be called for every config block.\n"
		"'data' is an optional object that will be passed back to the callback\n"
		"    function every time it is called.\n"
		"'name' is an optional identifier for this callback. The default name\n"
		"    is 'python.<module>'.\n"
		"    Every callback needs a unique identifier, so if you want to\n"
		"    register this callback multiple time from the same module you need\n"
		"    to specify a name here.\n"
		"'identifier' is the full identifier assigned to this callback.\n"
		"\n"
		"The callback function will be called with one or two parameters:\n"
		"config: A Config object.\n"
		"data: The optional data parameter passed to the register function.\n"
		"    If the parameter was omitted it will be omitted here, too.";

static char reg_read_doc[] = "register_read(callback[, interval][, data][, name]) -> identifier\n"
		"\n"
		"Register a callback function for reading data. It will just be called\n"
		"in a fixed interval to signal that it's time to dispatch new values.\n"
		"'callback' is a callable object that will be called every time something\n"
		"    is logged.\n"
		"'interval' is the number of seconds between between calls to the callback\n"
		"    function. Full float precision is supported here.\n"
		"'data' is an optional object that will be passed back to the callback\n"
		"    function every time it is called.\n"
		"'name' is an optional identifier for this callback. The default name\n"
		"    is 'python.<module>'.\n"
		"    Every callback needs a unique identifier, so if you want to\n"
		"    register this callback multiple time from the same module you need\n"
		"    to specify a name here.\n"
		"'identifier' is the full identifier assigned to this callback.\n"
		"\n"
		"The callback function will be called without parameters, except for\n"
		"data if it was supplied.";

static char reg_write_doc[] = "register_write(callback[, data][, name]) -> identifier\n"
		"\n"
		"Register a callback function to receive values dispatched by other plugins.\n"
		"'callback' is a callable object that will be called every time a value\n"
		"    is dispatched.\n"
		"'data' is an optional object that will be passed back to the callback\n"
		"    function every time it is called.\n"
		"'name' is an optional identifier for this callback. The default name\n"
		"    is 'python.<module>'.\n"
		"    Every callback needs a unique identifier, so if you want to\n"
		"    register this callback multiple time from the same module you need\n"
		"    to specify a name here.\n"
		"'identifier' is the full identifier assigned to this callback.\n"
		"\n"
		"The callback function will be called with one or two parameters:\n"
		"values: A Values object which is a copy of the dispatched values.\n"
		"data: The optional data parameter passed to the register function.\n"
		"    If the parameter was omitted it will be omitted here, too.";

static char reg_notification_doc[] = "register_notification(callback[, data][, name]) -> identifier\n"
		"\n"
		"Register a callback function for notifications.\n"
		"'callback' is a callable object that will be called every time a notification\n"
		"    is dispatched.\n"
		"'data' is an optional object that will be passed back to the callback\n"
		"    function every time it is called.\n"
		"'name' is an optional identifier for this callback. The default name\n"
		"    is 'python.<module>'.\n"
		"    Every callback needs a unique identifier, so if you want to\n"
		"    register this callback multiple time from the same module you need\n"
		"    to specify a name here.\n"
		"'identifier' is the full identifier assigned to this callback.\n"
		"\n"
		"The callback function will be called with one or two parameters:\n"
		"notification: A copy of the notification that was dispatched.\n"
		"data: The optional data parameter passed to the register function.\n"
		"    If the parameter was omitted it will be omitted here, too.";

static char reg_flush_doc[] = "register_flush(callback[, data][, name]) -> identifier\n"
		"\n"
		"Register a callback function for flush messages.\n"
		"'callback' is a callable object that will be called every time a plugin\n"
		"    requests a flush for either this or all plugins.\n"
		"'data' is an optional object that will be passed back to the callback\n"
		"    function every time it is called.\n"
		"'name' is an optional identifier for this callback. The default name\n"
		"    is 'python.<module>'.\n"
		"    Every callback needs a unique identifier, so if you want to\n"
		"    register this callback multiple time from the same module you need\n"
		"    to specify a name here.\n"
		"'identifier' is the full identifier assigned to this callback.\n"
		"\n"
		"The callback function will be called with two or three parameters:\n"
		"timeout: Indicates that only data older than 'timeout' seconds is to\n"
		"    be flushed.\n"
		"id: Specifies which values are to be flushed.\n"
		"data: The optional data parameter passed to the register function.\n"
		"    If the parameter was omitted it will be omitted here, too.";

static char reg_shutdown_doc[] = "register_shutdown(callback[, data][, name]) -> identifier\n"
		"\n"
		"Register a callback function for collectd shutdown.\n"
		"'callback' is a callable object that will be called once collectd is\n"
		"    shutting down.\n"
		"'data' is an optional object that will be passed back to the callback\n"
		"    function if it is called.\n"
		"'name' is an optional identifier for this callback. The default name\n"
		"    is 'python.<module>'.\n"
		"    Every callback needs a unique identifier, so if you want to\n"
		"    register this callback multiple time from the same module you need\n"
		"    to specify a name here.\n"
		"'identifier' is the full identifier assigned to this callback.\n"
		"\n"
		"The callback function will be called with no parameters except for\n"
		"    data if it was supplied.";


static int do_interactive = 0;

/* This is our global thread state. Python saves some stuff in thread-local
 * storage. So if we allow the interpreter to run in the background
 * (the scriptwriters might have created some threads from python), we have
 * to save the state so we can resume it later after shutdown. */

static PyThreadState *state;

static PyObject *sys_path, *cpy_format_exception;

static cpy_callback_t *cpy_config_callbacks;
static cpy_callback_t *cpy_init_callbacks;
static cpy_callback_t *cpy_shutdown_callbacks;

static void cpy_destroy_user_data(void *data) {
	cpy_callback_t *c = data;
	free(c->name);
	Py_DECREF(c->callback);
	Py_XDECREF(c->data);
	free(c);
}

/* You must hold the GIL to call this function!
 * But if you managed to extract the callback parameter then you probably already do. */

static void cpy_build_name(char *buf, size_t size, PyObject *callback, const char *name) {
	const char *module = NULL;
	PyObject *mod = NULL;
	
	if (name != NULL) {
		snprintf(buf, size, "python.%s", name);
		return;
	}
	
	mod = PyObject_GetAttrString(callback, "__module__"); /* New reference. */
	if (mod != NULL)
		module = cpy_unicode_or_bytes_to_string(&mod);
	
	if (module != NULL) {
		snprintf(buf, size, "python.%s", module);
		Py_XDECREF(mod);
		PyErr_Clear();
		return;
	}
	Py_XDECREF(mod);
	
	snprintf(buf, size, "python.%p", callback);
	PyErr_Clear();
}

void cpy_log_exception(const char *context) {
	int l = 0, i;
	const char *typename = NULL, *message = NULL;
	PyObject *type, *value, *traceback, *tn, *m, *list;
	
	PyErr_Fetch(&type, &value, &traceback);
	PyErr_NormalizeException(&type, &value, &traceback);
	if (type == NULL) return;
	tn = PyObject_GetAttrString(type, "__name__"); /* New reference. */
	m = PyObject_Str(value); /* New reference. */
	if (tn != NULL)
		typename = cpy_unicode_or_bytes_to_string(&tn);
	if (m != NULL)
		message = cpy_unicode_or_bytes_to_string(&m);
	if (typename == NULL)
		typename = "NamelessException";
	if (message == NULL)
		message = "N/A";
	Py_BEGIN_ALLOW_THREADS
	ERROR("Unhandled python exception in %s: %s: %s", context, typename, message);
	Py_END_ALLOW_THREADS
	Py_XDECREF(tn);
	Py_XDECREF(m);
	if (!cpy_format_exception) {
		PyErr_Clear();
		Py_XDECREF(type);
		Py_XDECREF(value);
		Py_XDECREF(traceback);
		return;
	}
	if (!traceback) {
		PyErr_Clear();
		return;
	}
	list = PyObject_CallFunction(cpy_format_exception, "NNN", type, value, traceback); /* New reference. */
	if (list)
		l = PyObject_Length(list);
	for (i = 0; i < l; ++i) {
		char *s;
		PyObject *line;
		
		line = PyList_GET_ITEM(list, i); /* Borrowed reference. */
		Py_INCREF(line);
		s = strdup(cpy_unicode_or_bytes_to_string(&line));
		Py_DECREF(line);
		if (s[strlen(s) - 1] == '\n')
			s[strlen(s) - 1] = 0;
		Py_BEGIN_ALLOW_THREADS
		ERROR("%s", s);
		Py_END_ALLOW_THREADS
		free(s);
	}
	Py_XDECREF(list);
	PyErr_Clear();
}

static int cpy_read_callback(user_data_t *data) {
	cpy_callback_t *c = data->data;
	PyObject *ret;

	CPY_LOCK_THREADS
		ret = PyObject_CallFunctionObjArgs(c->callback, c->data, (void *) 0); /* New reference. */
		if (ret == NULL) {
			cpy_log_exception("read callback");
		} else {
			Py_DECREF(ret);
		}
	CPY_RELEASE_THREADS
	if (ret == NULL)
		return 1;
	return 0;
}

static int cpy_write_callback(const data_set_t *ds, const value_list_t *value_list, user_data_t *data) {
	int i;
	cpy_callback_t *c = data->data;
	PyObject *ret, *list, *temp, *dict = NULL, *val;
	Values *v;

	CPY_LOCK_THREADS
		list = PyList_New(value_list->values_len); /* New reference. */
		if (list == NULL) {
			cpy_log_exception("write callback");
			CPY_RETURN_FROM_THREADS 0;
		}
		for (i = 0; i < value_list->values_len; ++i) {
			if (ds->ds[i].type == DS_TYPE_COUNTER) {
				if ((long) value_list->values[i].counter == value_list->values[i].counter)
					PyList_SetItem(list, i, PyInt_FromLong(value_list->values[i].counter));
				else
					PyList_SetItem(list, i, PyLong_FromUnsignedLongLong(value_list->values[i].counter));
			} else if (ds->ds[i].type == DS_TYPE_GAUGE) {
				PyList_SetItem(list, i, PyFloat_FromDouble(value_list->values[i].gauge));
			} else if (ds->ds[i].type == DS_TYPE_DERIVE) {
				if ((long) value_list->values[i].derive == value_list->values[i].derive)
					PyList_SetItem(list, i, PyInt_FromLong(value_list->values[i].derive));
				else
					PyList_SetItem(list, i, PyLong_FromLongLong(value_list->values[i].derive));
			} else if (ds->ds[i].type == DS_TYPE_ABSOLUTE) {
				if ((long) value_list->values[i].absolute == value_list->values[i].absolute)
					PyList_SetItem(list, i, PyInt_FromLong(value_list->values[i].absolute));
				else
					PyList_SetItem(list, i, PyLong_FromUnsignedLongLong(value_list->values[i].absolute));
			} else {
				Py_BEGIN_ALLOW_THREADS
				ERROR("cpy_write_callback: Unknown value type %d.", ds->ds[i].type);
				Py_END_ALLOW_THREADS
				Py_DECREF(list);
				CPY_RETURN_FROM_THREADS 0;
			}
			if (PyErr_Occurred() != NULL) {
				cpy_log_exception("value building for write callback");
				Py_DECREF(list);
				CPY_RETURN_FROM_THREADS 0;
			}
		}
		dict = PyDict_New();
		if (value_list->meta) {
			int i, num;
			char **table;
			meta_data_t *meta = value_list->meta;

			num = meta_data_toc(meta, &table);
			for (i = 0; i < num; ++i) {
				int type;
				char *string;
				int64_t si;
				uint64_t ui;
				double d;
				_Bool b;
				
				type = meta_data_type(meta, table[i]);
				if (type == MD_TYPE_STRING) {
					if (meta_data_get_string(meta, table[i], &string))
						continue;
					temp = cpy_string_to_unicode_or_bytes(string);
					free(string);
					PyDict_SetItemString(dict, table[i], temp);
					Py_XDECREF(temp);
				} else if (type == MD_TYPE_SIGNED_INT) {
					if (meta_data_get_signed_int(meta, table[i], &si))
						continue;
					temp = PyObject_CallFunctionObjArgs((void *) &SignedType, PyLong_FromLongLong(si), (void *) 0);
					PyDict_SetItemString(dict, table[i], temp);
					Py_XDECREF(temp);
				} else if (type == MD_TYPE_UNSIGNED_INT) {
					if (meta_data_get_unsigned_int(meta, table[i], &ui))
						continue;
					temp = PyObject_CallFunctionObjArgs((void *) &UnsignedType, PyLong_FromUnsignedLongLong(ui), (void *) 0);
					PyDict_SetItemString(dict, table[i], temp);
					Py_XDECREF(temp);
				} else if (type == MD_TYPE_DOUBLE) {
					if (meta_data_get_double(meta, table[i], &d))
						continue;
					temp = PyFloat_FromDouble(d);
					PyDict_SetItemString(dict, table[i], temp);
					Py_XDECREF(temp);
				} else if (type == MD_TYPE_BOOLEAN) {
					if (meta_data_get_boolean(meta, table[i], &b))
						continue;
					if (b)
						PyDict_SetItemString(dict, table[i], Py_True);
					else
						PyDict_SetItemString(dict, table[i], Py_False);
				}
				free(table[i]);
			}
			free(table);
		}
		val = Values_New(); /* New reference. */
		v = (Values *) val; 
		sstrncpy(v->data.host, value_list->host, sizeof(v->data.host));
		sstrncpy(v->data.type, value_list->type, sizeof(v->data.type));
		sstrncpy(v->data.type_instance, value_list->type_instance, sizeof(v->data.type_instance));
		sstrncpy(v->data.plugin, value_list->plugin, sizeof(v->data.plugin));
		sstrncpy(v->data.plugin_instance, value_list->plugin_instance, sizeof(v->data.plugin_instance));
		v->data.time = CDTIME_T_TO_DOUBLE(value_list->time);
		v->interval = CDTIME_T_TO_DOUBLE(value_list->interval);
		Py_CLEAR(v->values);
		v->values = list;
		Py_CLEAR(v->meta);
		v->meta = dict;
		ret = PyObject_CallFunctionObjArgs(c->callback, v, c->data, (void *) 0); /* New reference. */
		Py_XDECREF(val);
		if (ret == NULL) {
			cpy_log_exception("write callback");
		} else {
			Py_DECREF(ret);
		}
	CPY_RELEASE_THREADS
	return 0;
}

static int cpy_notification_callback(const notification_t *notification, user_data_t *data) {
	cpy_callback_t *c = data->data;
	PyObject *ret, *notify;
	Notification *n;

	CPY_LOCK_THREADS
		notify = Notification_New(); /* New reference. */
		n = (Notification *) notify;
		sstrncpy(n->data.host, notification->host, sizeof(n->data.host));
		sstrncpy(n->data.type, notification->type, sizeof(n->data.type));
		sstrncpy(n->data.type_instance, notification->type_instance, sizeof(n->data.type_instance));
		sstrncpy(n->data.plugin, notification->plugin, sizeof(n->data.plugin));
		sstrncpy(n->data.plugin_instance, notification->plugin_instance, sizeof(n->data.plugin_instance));
		n->data.time = CDTIME_T_TO_DOUBLE(notification->time);
		sstrncpy(n->message, notification->message, sizeof(n->message));
		n->severity = notification->severity;
		ret = PyObject_CallFunctionObjArgs(c->callback, n, c->data, (void *) 0); /* New reference. */
		Py_XDECREF(notify);
		if (ret == NULL) {
			cpy_log_exception("notification callback");
		} else {
			Py_DECREF(ret);
		}
	CPY_RELEASE_THREADS
	return 0;
}

static void cpy_log_callback(int severity, const char *message, user_data_t *data) {
	cpy_callback_t * c = data->data;
	PyObject *ret, *text;

	CPY_LOCK_THREADS
	text = cpy_string_to_unicode_or_bytes(message);
	if (c->data == NULL)
		ret = PyObject_CallFunction(c->callback, "iN", severity, text); /* New reference. */
	else
		ret = PyObject_CallFunction(c->callback, "iNO", severity, text, c->data); /* New reference. */

	if (ret == NULL) {
		/* FIXME */
		/* Do we really want to trigger a log callback because a log callback failed?
		 * Probably not. */
		PyErr_Print();
		/* In case someone wanted to be clever, replaced stderr and failed at that. */
		PyErr_Clear();
	} else {
		Py_DECREF(ret);
	}
	CPY_RELEASE_THREADS
}

static void cpy_flush_callback(int timeout, const char *id, user_data_t *data) {
	cpy_callback_t * c = data->data;
	PyObject *ret, *text;

	CPY_LOCK_THREADS
	text = cpy_string_to_unicode_or_bytes(id);
	if (c->data == NULL)
		ret = PyObject_CallFunction(c->callback, "iN", timeout, text); /* New reference. */
	else
		ret = PyObject_CallFunction(c->callback, "iNO", timeout, text, c->data); /* New reference. */

	if (ret == NULL) {
		cpy_log_exception("flush callback");
	} else {
		Py_DECREF(ret);
	}
	CPY_RELEASE_THREADS
}

static PyObject *cpy_register_generic(cpy_callback_t **list_head, PyObject *args, PyObject *kwds) {
	char buf[512];
	cpy_callback_t *c;
	const char *name = NULL;
	PyObject *callback = NULL, *data = NULL, *mod = NULL;
	static char *kwlist[] = {"callback", "data", "name", NULL};
	
	if (PyArg_ParseTupleAndKeywords(args, kwds, "O|Oet", kwlist, &callback, &data, NULL, &name) == 0) return NULL;
	if (PyCallable_Check(callback) == 0) {
		PyErr_SetString(PyExc_TypeError, "callback needs a be a callable object.");
		return NULL;
	}
	cpy_build_name(buf, sizeof(buf), callback, name);

	Py_INCREF(callback);
	Py_XINCREF(data);
	c = malloc(sizeof(*c));
	c->name = strdup(buf);
	c->callback = callback;
	c->data = data;
	c->next = *list_head;
	*list_head = c;
	Py_XDECREF(mod);
	return cpy_string_to_unicode_or_bytes(buf);
}

static PyObject *cpy_flush(cpy_callback_t **list_head, PyObject *args, PyObject *kwds) {
	int timeout = -1;
	const char *plugin = NULL, *identifier = NULL;
	static char *kwlist[] = {"plugin", "timeout", "identifier", NULL};
	
	if (PyArg_ParseTupleAndKeywords(args, kwds, "|etiet", kwlist, NULL, &plugin, &timeout, NULL, &identifier) == 0) return NULL;
	Py_BEGIN_ALLOW_THREADS
	plugin_flush(plugin, timeout, identifier);
	Py_END_ALLOW_THREADS
	Py_RETURN_NONE;
}

static PyObject *cpy_register_config(PyObject *self, PyObject *args, PyObject *kwds) {
	return cpy_register_generic(&cpy_config_callbacks, args, kwds);
}

static PyObject *cpy_register_init(PyObject *self, PyObject *args, PyObject *kwds) {
	return cpy_register_generic(&cpy_init_callbacks, args, kwds);
}

typedef int reg_function_t(const char *name, void *callback, void *data);

static PyObject *cpy_register_generic_userdata(void *reg, void *handler, PyObject *args, PyObject *kwds) {
	char buf[512];
	reg_function_t *register_function = (reg_function_t *) reg;
	cpy_callback_t *c = NULL;
	user_data_t *user_data = NULL;
	const char *name = NULL;
	PyObject *callback = NULL, *data = NULL;
	static char *kwlist[] = {"callback", "data", "name", NULL};
	
	if (PyArg_ParseTupleAndKeywords(args, kwds, "O|Oet", kwlist, &callback, &data, NULL, &name) == 0) return NULL;
	if (PyCallable_Check(callback) == 0) {
		PyErr_SetString(PyExc_TypeError, "callback needs a be a callable object.");
		return NULL;
	}
	cpy_build_name(buf, sizeof(buf), callback, name);
	
	Py_INCREF(callback);
	Py_XINCREF(data);
	c = malloc(sizeof(*c));
	c->name = strdup(buf);
	c->callback = callback;
	c->data = data;
	c->next = NULL;
	user_data = malloc(sizeof(*user_data));
	user_data->free_func = cpy_destroy_user_data;
	user_data->data = c;
	register_function(buf, handler, user_data);
	return cpy_string_to_unicode_or_bytes(buf);
}

static PyObject *cpy_register_read(PyObject *self, PyObject *args, PyObject *kwds) {
	char buf[512];
	cpy_callback_t *c = NULL;
	user_data_t *user_data = NULL;
	double interval = 0;
	const char *name = NULL;
	PyObject *callback = NULL, *data = NULL;
	struct timespec ts;
	static char *kwlist[] = {"callback", "interval", "data", "name", NULL};
	
	if (PyArg_ParseTupleAndKeywords(args, kwds, "O|dOet", kwlist, &callback, &interval, &data, NULL, &name) == 0) return NULL;
	if (PyCallable_Check(callback) == 0) {
		PyErr_SetString(PyExc_TypeError, "callback needs a be a callable object.");
		return NULL;
	}
	cpy_build_name(buf, sizeof(buf), callback, name);
	
	Py_INCREF(callback);
	Py_XINCREF(data);
	c = malloc(sizeof(*c));
	c->name = strdup(buf);
	c->callback = callback;
	c->data = data;
	c->next = NULL;
	user_data = malloc(sizeof(*user_data));
	user_data->free_func = cpy_destroy_user_data;
	user_data->data = c;
	ts.tv_sec = interval;
	ts.tv_nsec = (interval - ts.tv_sec) * 1000000000;
	plugin_register_complex_read(/* group = */ NULL, buf,
			cpy_read_callback, &ts, user_data);
	return cpy_string_to_unicode_or_bytes(buf);
}

static PyObject *cpy_register_log(PyObject *self, PyObject *args, PyObject *kwds) {
	return cpy_register_generic_userdata((void *) plugin_register_log,
			(void *) cpy_log_callback, args, kwds);
}

static PyObject *cpy_register_write(PyObject *self, PyObject *args, PyObject *kwds) {
	return cpy_register_generic_userdata((void *) plugin_register_write,
			(void *) cpy_write_callback, args, kwds);
}

static PyObject *cpy_register_notification(PyObject *self, PyObject *args, PyObject *kwds) {
	return cpy_register_generic_userdata((void *) plugin_register_notification,
			(void *) cpy_notification_callback, args, kwds);
}

static PyObject *cpy_register_flush(PyObject *self, PyObject *args, PyObject *kwds) {
	return cpy_register_generic_userdata((void *) plugin_register_flush,
			(void *) cpy_flush_callback, args, kwds);
}

static PyObject *cpy_register_shutdown(PyObject *self, PyObject *args, PyObject *kwds) {
	return cpy_register_generic(&cpy_shutdown_callbacks, args, kwds);
}

static PyObject *cpy_error(PyObject *self, PyObject *args) {
	const char *text;
	if (PyArg_ParseTuple(args, "et", NULL, &text) == 0) return NULL;
	Py_BEGIN_ALLOW_THREADS
	plugin_log(LOG_ERR, "%s", text);
	Py_END_ALLOW_THREADS
	Py_RETURN_NONE;
}

static PyObject *cpy_warning(PyObject *self, PyObject *args) {
	const char *text;
	if (PyArg_ParseTuple(args, "et", NULL, &text) == 0) return NULL;
	Py_BEGIN_ALLOW_THREADS
	plugin_log(LOG_WARNING, "%s", text);
	Py_END_ALLOW_THREADS
	Py_RETURN_NONE;
}

static PyObject *cpy_notice(PyObject *self, PyObject *args) {
	const char *text;
	if (PyArg_ParseTuple(args, "et", NULL, &text) == 0) return NULL;
	Py_BEGIN_ALLOW_THREADS
	plugin_log(LOG_NOTICE, "%s", text);
	Py_END_ALLOW_THREADS
	Py_RETURN_NONE;
}

static PyObject *cpy_info(PyObject *self, PyObject *args) {
	const char *text;
	if (PyArg_ParseTuple(args, "et", NULL, &text) == 0) return NULL;
	Py_BEGIN_ALLOW_THREADS
	plugin_log(LOG_INFO, "%s", text);
	Py_END_ALLOW_THREADS
	Py_RETURN_NONE;
}

static PyObject *cpy_debug(PyObject *self, PyObject *args) {
#ifdef COLLECT_DEBUG
	const char *text;
	if (PyArg_ParseTuple(args, "et", NULL, &text) == 0) return NULL;
	Py_BEGIN_ALLOW_THREADS
	plugin_log(LOG_DEBUG, "%s", text);
	Py_END_ALLOW_THREADS
#endif
	Py_RETURN_NONE;
}

static PyObject *cpy_unregister_generic(cpy_callback_t **list_head, PyObject *arg, const char *desc) {
	char buf[512];
	const char *name;
	cpy_callback_t *prev = NULL, *tmp;

	Py_INCREF(arg);
	name = cpy_unicode_or_bytes_to_string(&arg);
	if (name == NULL) {
		PyErr_Clear();
		if (!PyCallable_Check(arg)) {
			PyErr_SetString(PyExc_TypeError, "This function needs a string or a callable object as its only parameter.");
			Py_DECREF(arg);
			return NULL;
		}
		cpy_build_name(buf, sizeof(buf), arg, NULL);
		name = buf;
	}
	for (tmp = *list_head; tmp; prev = tmp, tmp = tmp->next)
		if (strcmp(name, tmp->name) == 0)
			break;
	
	Py_DECREF(arg);
	if (tmp == NULL) {
		PyErr_Format(PyExc_RuntimeError, "Unable to unregister %s callback '%s'.", desc, name);
		return NULL;
	}
	/* Yes, this is actually save. To call this function the caller has to
	 * hold the GIL. Well, save as long as there is only one GIL anyway ... */
	if (prev == NULL)
		*list_head = tmp->next;
	else
		prev->next = tmp->next;
	cpy_destroy_user_data(tmp);
	Py_RETURN_NONE;
}

typedef int cpy_unregister_function_t(const char *name);

static PyObject *cpy_unregister_generic_userdata(cpy_unregister_function_t *unreg, PyObject *arg, const char *desc) {
	char buf[512];
	const char *name;

	Py_INCREF(arg);
	name = cpy_unicode_or_bytes_to_string(&arg);
	if (name == NULL) {
		PyErr_Clear();
		if (!PyCallable_Check(arg)) {
			PyErr_SetString(PyExc_TypeError, "This function needs a string or a callable object as its only parameter.");
			Py_DECREF(arg);
			return NULL;
		}
		cpy_build_name(buf, sizeof(buf), arg, NULL);
		name = buf;
	}
	if (unreg(name) == 0) {
		Py_DECREF(arg);
		Py_RETURN_NONE;
	}
	PyErr_Format(PyExc_RuntimeError, "Unable to unregister %s callback '%s'.", desc, name);
	Py_DECREF(arg);
	return NULL;
}

static PyObject *cpy_unregister_log(PyObject *self, PyObject *arg) {
	return cpy_unregister_generic_userdata(plugin_unregister_log, arg, "log");
}

static PyObject *cpy_unregister_init(PyObject *self, PyObject *arg) {
	return cpy_unregister_generic(&cpy_init_callbacks, arg, "init");
}

static PyObject *cpy_unregister_config(PyObject *self, PyObject *arg) {
	return cpy_unregister_generic(&cpy_config_callbacks, arg, "config");
}

static PyObject *cpy_unregister_read(PyObject *self, PyObject *arg) {
	return cpy_unregister_generic_userdata(plugin_unregister_read, arg, "read");
}

static PyObject *cpy_unregister_write(PyObject *self, PyObject *arg) {
	return cpy_unregister_generic_userdata(plugin_unregister_write, arg, "write");
}

static PyObject *cpy_unregister_notification(PyObject *self, PyObject *arg) {
	return cpy_unregister_generic_userdata(plugin_unregister_notification, arg, "notification");
}

static PyObject *cpy_unregister_flush(PyObject *self, PyObject *arg) {
	return cpy_unregister_generic_userdata(plugin_unregister_flush, arg, "flush");
}

static PyObject *cpy_unregister_shutdown(PyObject *self, PyObject *arg) {
	return cpy_unregister_generic(&cpy_shutdown_callbacks, arg, "shutdown");
}

static PyMethodDef cpy_methods[] = {
	{"debug", cpy_debug, METH_VARARGS, log_doc},
	{"info", cpy_info, METH_VARARGS, log_doc},
	{"notice", cpy_notice, METH_VARARGS, log_doc},
	{"warning", cpy_warning, METH_VARARGS, log_doc},
	{"error", cpy_error, METH_VARARGS, log_doc},
	{"flush", (PyCFunction) cpy_flush, METH_VARARGS | METH_KEYWORDS, flush_doc},
	{"register_log", (PyCFunction) cpy_register_log, METH_VARARGS | METH_KEYWORDS, reg_log_doc},
	{"register_init", (PyCFunction) cpy_register_init, METH_VARARGS | METH_KEYWORDS, reg_init_doc},
	{"register_config", (PyCFunction) cpy_register_config, METH_VARARGS | METH_KEYWORDS, reg_config_doc},
	{"register_read", (PyCFunction) cpy_register_read, METH_VARARGS | METH_KEYWORDS, reg_read_doc},
	{"register_write", (PyCFunction) cpy_register_write, METH_VARARGS | METH_KEYWORDS, reg_write_doc},
	{"register_notification", (PyCFunction) cpy_register_notification, METH_VARARGS | METH_KEYWORDS, reg_notification_doc},
	{"register_flush", (PyCFunction) cpy_register_flush, METH_VARARGS | METH_KEYWORDS, reg_flush_doc},
	{"register_shutdown", (PyCFunction) cpy_register_shutdown, METH_VARARGS | METH_KEYWORDS, reg_shutdown_doc},
	{"unregister_log", cpy_unregister_log, METH_O, unregister_doc},
	{"unregister_init", cpy_unregister_init, METH_O, unregister_doc},
	{"unregister_config", cpy_unregister_config, METH_O, unregister_doc},
	{"unregister_read", cpy_unregister_read, METH_O, unregister_doc},
	{"unregister_write", cpy_unregister_write, METH_O, unregister_doc},
	{"unregister_notification", cpy_unregister_notification, METH_O, unregister_doc},
	{"unregister_flush", cpy_unregister_flush, METH_O, unregister_doc},
	{"unregister_shutdown", cpy_unregister_shutdown, METH_O, unregister_doc},
	{0, 0, 0, 0}
};

static int cpy_shutdown(void) {
	cpy_callback_t *c;
	PyObject *ret;
	
	/* This can happen if the module was loaded but not configured. */
	if (state != NULL)
		PyEval_RestoreThread(state);

	for (c = cpy_shutdown_callbacks; c; c = c->next) {
		ret = PyObject_CallFunctionObjArgs(c->callback, c->data, (void *) 0); /* New reference. */
		if (ret == NULL)
			cpy_log_exception("shutdown callback");
		else
			Py_DECREF(ret);
	}
	PyErr_Print();
	Py_Finalize();
	return 0;
}

static void cpy_int_handler(int sig) {
	return;
}

static void *cpy_interactive(void *data) {
	sigset_t sigset;
	struct sigaction sig_int_action, old;
	
	/* Signal handler in a plugin? Bad stuff, but the best way to
	 * handle it I guess. In an interactive session people will
	 * press Ctrl+C at some time, which will generate a SIGINT.
	 * This will cause collectd to shutdown, thus killing the
	 * interactive interpreter, and leaving the terminal in a
	 * mess. Chances are, this isn't what the user wanted to do.
	 * 
	 * So this is the plan:
	 * 1. Block SIGINT in the main thread.
	 * 2. Install our own signal handler that does nothing.
	 * 3. Unblock SIGINT in the interactive thread.
	 *
	 * This will make sure that SIGINT won't kill collectd but
	 * still interrupt syscalls like sleep and pause.
	 * It does not raise a KeyboardInterrupt exception because so
	 * far nobody managed to figure out how to do that. */
	memset (&sig_int_action, '\0', sizeof (sig_int_action));
	sig_int_action.sa_handler = cpy_int_handler;
	sigaction (SIGINT, &sig_int_action, &old);
	
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	pthread_sigmask(SIG_UNBLOCK, &sigset, NULL);
	PyEval_AcquireThread(state);
	if (PyImport_ImportModule("readline") == NULL) {
		/* This interactive session will suck. */
		cpy_log_exception("interactive session init");
	}
	PyRun_InteractiveLoop(stdin, "<stdin>");
	PyErr_Print();
	PyEval_ReleaseThread(state);
	NOTICE("python: Interactive interpreter exited, stopping collectd ...");
	/* Restore the original collectd SIGINT handler and raise SIGINT.
	 * The main thread still has SIGINT blocked and there's nothing we
	 * can do about that so this thread will handle it. But that's not
	 * important, except that it won't interrupt the main loop and so
	 * it might take a few seconds before collectd really shuts down. */
	sigaction (SIGINT, &old, NULL);
	raise(SIGINT);
	pause();
	return NULL;
}

static int cpy_init(void) {
	cpy_callback_t *c;
	PyObject *ret;
	static pthread_t thread;
	sigset_t sigset;
	
	if (!Py_IsInitialized()) {
		WARNING("python: Plugin loaded but not configured.");
		plugin_unregister_shutdown("python");
		return 0;
	}
	PyEval_InitThreads();
	/* Now it's finally OK to use python threads. */
	for (c = cpy_init_callbacks; c; c = c->next) {
		ret = PyObject_CallFunctionObjArgs(c->callback, c->data, (void *) 0); /* New reference. */
		if (ret == NULL)
			cpy_log_exception("init callback");
		else
			Py_DECREF(ret);
	}
	sigemptyset(&sigset);
	sigaddset(&sigset, SIGINT);
	pthread_sigmask(SIG_BLOCK, &sigset, NULL);
	state = PyEval_SaveThread();
	if (do_interactive) {
		if (pthread_create(&thread, NULL, cpy_interactive, NULL)) {
			ERROR("python: Error creating thread for interactive interpreter.");
		}
	}

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
			PyTuple_SET_ITEM(values, i, cpy_string_to_unicode_or_bytes(ci->values[i].value.string));
		} else if (ci->values[i].type == OCONFIG_TYPE_NUMBER) {
			PyTuple_SET_ITEM(values, i, PyFloat_FromDouble(ci->values[i].value.number));
		} else if (ci->values[i].type == OCONFIG_TYPE_BOOLEAN) {
			PyTuple_SET_ITEM(values, i, PyBool_FromLong(ci->values[i].value.boolean));
		}
	}
	
	tmp = cpy_string_to_unicode_or_bytes(ci->key);
	item = PyObject_CallFunction((void *) &ConfigType, "NONO", tmp, parent, values, Py_None);
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

#ifdef IS_PY3K
static struct PyModuleDef collectdmodule = {
	PyModuleDef_HEAD_INIT,
	"collectd",   /* name of module */
	"The python interface to collectd", /* module documentation, may be NULL */
	-1,
	cpy_methods
};

PyMODINIT_FUNC PyInit_collectd(void) {
	return PyModule_Create(&collectdmodule);
}
#endif

static int cpy_init_python() {
	char *argv = "";
	PyObject *sys;
	PyObject *module;

#ifdef IS_PY3K
	/* Add a builtin module, before Py_Initialize */
	PyImport_AppendInittab("collectd", PyInit_collectd);
#endif
	
	Py_Initialize();
	
	PyType_Ready(&ConfigType);
	PyType_Ready(&PluginDataType);
	ValuesType.tp_base = &PluginDataType;
	PyType_Ready(&ValuesType);
	NotificationType.tp_base = &PluginDataType;
	PyType_Ready(&NotificationType);
	SignedType.tp_base = &PyLong_Type;
	PyType_Ready(&SignedType);
	UnsignedType.tp_base = &PyLong_Type;
	PyType_Ready(&UnsignedType);
	sys = PyImport_ImportModule("sys"); /* New reference. */
	if (sys == NULL) {
		cpy_log_exception("python initialization");
		return 1;
	}
	sys_path = PyObject_GetAttrString(sys, "path"); /* New reference. */
	Py_DECREF(sys);
	if (sys_path == NULL) {
		cpy_log_exception("python initialization");
		return 1;
	}
	PySys_SetArgv(1, &argv);
	PyList_SetSlice(sys_path, 0, 1, NULL);

#ifdef IS_PY3K
	module = PyImport_ImportModule("collectd");
#else
	module = Py_InitModule("collectd", cpy_methods); /* Borrowed reference. */
#endif
	PyModule_AddObject(module, "Config", (void *) &ConfigType); /* Steals a reference. */
	PyModule_AddObject(module, "Values", (void *) &ValuesType); /* Steals a reference. */
	PyModule_AddObject(module, "Notification", (void *) &NotificationType); /* Steals a reference. */
	PyModule_AddObject(module, "Signed", (void *) &SignedType); /* Steals a reference. */
	PyModule_AddObject(module, "Unsigned", (void *) &UnsignedType); /* Steals a reference. */
	PyModule_AddIntConstant(module, "LOG_DEBUG", LOG_DEBUG);
	PyModule_AddIntConstant(module, "LOG_INFO", LOG_INFO);
	PyModule_AddIntConstant(module, "LOG_NOTICE", LOG_NOTICE);
	PyModule_AddIntConstant(module, "LOG_WARNING", LOG_WARNING);
	PyModule_AddIntConstant(module, "LOG_ERROR", LOG_ERR);
	PyModule_AddIntConstant(module, "NOTIF_FAILURE", NOTIF_FAILURE);
	PyModule_AddIntConstant(module, "NOTIF_WARNING", NOTIF_WARNING);
	PyModule_AddIntConstant(module, "NOTIF_OKAY", NOTIF_OKAY);
	return 0;
}

static int cpy_config(oconfig_item_t *ci) {
	int i;
	PyObject *tb;

	/* Ok in theory we shouldn't do initialization at this point
	 * but we have to. In order to give python scripts a chance
	 * to register a config callback we need to be able to execute
	 * python code during the config callback so we have to start
	 * the interpreter here. */
	/* Do *not* use the python "thread" module at this point! */

	if (!Py_IsInitialized() && cpy_init_python()) return 1;

	for (i = 0; i < ci->children_num; ++i) {
		oconfig_item_t *item = ci->children + i;
		
		if (strcasecmp(item->key, "Interactive") == 0) {
			if (item->values_num != 1 || item->values[0].type != OCONFIG_TYPE_BOOLEAN)
				continue;
			do_interactive = item->values[0].value.boolean;
		} else if (strcasecmp(item->key, "Encoding") == 0) {
			if (item->values_num != 1 || item->values[0].type != OCONFIG_TYPE_STRING)
				continue;
			/* Why is this even necessary? And undocumented? */
			if (PyUnicode_SetDefaultEncoding(item->values[0].value.string))
				cpy_log_exception("setting default encoding");
		} else if (strcasecmp(item->key, "LogTraces") == 0) {
			if (item->values_num != 1 || item->values[0].type != OCONFIG_TYPE_BOOLEAN)
				continue;
			if (!item->values[0].value.boolean) {
				Py_XDECREF(cpy_format_exception);
				cpy_format_exception = NULL;
				continue;
			}
			if (cpy_format_exception)
				continue;
			tb = PyImport_ImportModule("traceback"); /* New reference. */
			if (tb == NULL) {
				cpy_log_exception("python initialization");
				continue;
			}
			cpy_format_exception = PyObject_GetAttrString(tb, "format_exception"); /* New reference. */
			Py_DECREF(tb);
			if (cpy_format_exception == NULL)
				cpy_log_exception("python initialization");
		} else if (strcasecmp(item->key, "ModulePath") == 0) {
			char *dir = NULL;
			PyObject *dir_object;
			
			if (cf_util_get_string(item, &dir) != 0) 
				continue;
			dir_object = cpy_string_to_unicode_or_bytes(dir); /* New reference. */
			if (dir_object == NULL) {
				ERROR("python plugin: Unable to convert \"%s\" to "
				      "a python object.", dir);
				free(dir);
				cpy_log_exception("python initialization");
				continue;
			}
			if (PyList_Append(sys_path, dir_object) != 0) {
				ERROR("python plugin: Unable to append \"%s\" to "
				      "python module path.", dir);
				cpy_log_exception("python initialization");
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
				cpy_log_exception("importing module");
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
				if (strcasecmp(c->name + 7, name) == 0)
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
			if (c->data == NULL)
				ret = PyObject_CallFunction(c->callback, "N",
					cpy_oconfig_to_pyconfig(item, NULL)); /* New reference. */
			else
				ret = PyObject_CallFunction(c->callback, "NO",
					cpy_oconfig_to_pyconfig(item, NULL), c->data); /* New reference. */
			if (ret == NULL)
				cpy_log_exception("loading module");
			else
				Py_DECREF(ret);
		} else {
			WARNING("python plugin: Ignoring unknown config key \"%s\".", item->key);
		}
	}
	return 0;
}

void module_register(void) {
	plugin_register_complex_config("python", cpy_config);
	plugin_register_init("python", cpy_init);
	plugin_register_shutdown("python", cpy_shutdown);
}
