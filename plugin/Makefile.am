plugin_LTLIBRARIES = libgstpython.la

INCLUDES = $(PYGOBJECT_CFLAGS) $(GST_CFLAGS)\
	-DPYTHON_VERSION=\"$(PYTHON_VERSION)\"		\
	-DPY_LIB_LOC="\"$(PYTHON_LIB_LOC)\""		\
	-DPY_LIB_SUFFIX=$(PYTHON_LIB_SUFFIX) \
	$(PYTHON_INCLUDES)

libgstpython_la_SOURCES = gstpythonplugin.c
libgstpython_la_LDFLAGS = -module -avoid-version
libgstpython_la_LIBADD = $(GST_LIBS)
