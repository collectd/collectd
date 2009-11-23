/* These two macros are basicly Py_BEGIN_ALLOW_THREADS and Py_BEGIN_ALLOW_THREADS
 * from the other direction. If a Python thread calls a C function
 * Py_BEGIN_ALLOW_THREADS is used to allow other python threads to run because
 * we don't intend to call any Python functions.
 *
 * These two macros are used whenever a C thread intends to call some Python
 * function, usually because some registered callback was triggered.
 * Just like Py_BEGIN_ALLOW_THREADS it opens a block so these macros have to be
 * used in pairs. They aquire the GIL, create a new Python thread state and swap
 * the current thread state with the new one. This means this thread is now allowed
 * to execute Python code. */

#define CPY_LOCK_THREADS {\
	PyGILState_STATE gil_state;\
	gil_state = PyGILState_Ensure();

#define CPY_RETURN_FROM_THREADS \
	PyGILState_Release(gil_state);\
	return

#define CPY_RELEASE_THREADS \
	PyGILState_Release(gil_state);\
}

/* Python 2.4 has this macro, older versions do not. */
#ifndef Py_VISIT
#define Py_VISIT(o) do {\
	int _vret;\
	if ((o) != NULL) {\
		_vret = visit((o), arg);\
		if (_vret != 0)\
		return _vret;\
	}\
} while (0)
#endif

/* Python 2.4 has this macro, older versions do not. */
#ifndef Py_CLEAR
#define Py_CLEAR(o) do {\
	PyObject *tmp = o;\
	(o) = NULL;\
	Py_XDECREF(tmp);\
} while (0)
#endif

typedef struct {
	PyObject_HEAD        /* No semicolon! */
	PyObject *parent;    /* Config */
	PyObject *key;       /* String */
	PyObject *values;    /* Sequence */
	PyObject *children;  /* Sequence */
} Config;

PyTypeObject ConfigType;

typedef struct {
	PyObject_HEAD        /* No semicolon! */
	double time;
	char host[DATA_MAX_NAME_LEN];
	char plugin[DATA_MAX_NAME_LEN];
	char plugin_instance[DATA_MAX_NAME_LEN];
	char type[DATA_MAX_NAME_LEN];
	char type_instance[DATA_MAX_NAME_LEN];
} PluginData;

PyTypeObject PluginDataType;

typedef struct {
	PluginData data;
	PyObject *values;    /* Sequence */
	int interval;
} Values;

PyTypeObject ValuesType;
