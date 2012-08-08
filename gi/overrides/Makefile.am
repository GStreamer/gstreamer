common_cflags = $(PYGOBJECT_CFLAGS) $(GST_CFLAGS) -fno-strict-aliasing
common_libadd = $(GST_LIBS) $(PYGOBJECT_LIBS)
common_ldflags = -module -avoid-version

# We install everything in the gi/overrides folder
pygioverridesdir = $(PYGI_OVERRIDES_DIR)
pygioverrides_PYTHON = Gst.py

pygioverridesexecdir = $(PYGI_OVERRIDES_DIR)

EXTRA_DIST = Gst.py

INCLUDES = $(PYTHON_INCLUDES)

pygioverridesexec_LTLIBRARIES = _gi_gst.la

_gi_gst_la_SOURCES = gstmodule.c
_gi_gst_la_CFLAGS = $(common_cflags)
_gi_gst_la_LIBADD = $(common_libadd)
_gi_gst_la_LDFLAGS = $(common_ldflags)
