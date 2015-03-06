plugin_LTLIBRARIES = libgstpythonplugin.la

INCLUDES = $(PYGOBJECT_CFLAGS) $(GST_CFLAGS)\
-DPYTHON_VERSION=\"$(PYTHON_VERSION)\"		\
	-DPY_LIB_LOC="\"$(PYTHON_LIB_LOC)\""		\
	-DPY_ABI_FLAGS="\"$(PYTHON_ABI_FLAGS)\""		\
	-DPY_LIB_SUFFIX=$(PYTHON_LIB_SUFFIX) \
	$(PYTHON_INCLUDES)

libgstpythonplugin_la_SOURCES = gstpythonplugin.c 
libgstpythonplugin_la_LDFLAGS = -avoid-version -shrext $(PYTHON_SO)
libgstpythonplugin_la_LIBADD =  $(PYTHON_LIBS) $(PYGOBJECT_LIBS) $(GST_LIBS)
libgstpythonplugin_la_CFLAGS = $(GST_CFLAGS) $(PYGOBJECT_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(PYTHON_INCLUDES)
