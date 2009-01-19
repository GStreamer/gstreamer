plugin_LTLIBRARIES = libgstpython.la

INCLUDES = $(PYGOBJECT_CFLAGS) $(GST_CFLAGS)\
	-DPYTHON_VERSION=\"$(PYTHON_VERSION)\"		\
	-DPY_LIB_LOC="\"$(PYTHON_LIB_LOC)\""		\
	$(PYTHON_INCLUDES)

libgstpython_la_SOURCES = gstpythonplugin.c
#libgstpython_la_CFLAGS = $(PYGTK_CFLAGS) $(GST_CFLAGS)
#libgstpython_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -avoid-version -module # $(PYTHON_LIBS) -lpython2.4
libgstpython_la_LDFLAGS = -module -avoid-version
libgstpython_la_LIBADD = $(GST_LIBS) $(PYTHON_LIBS)
