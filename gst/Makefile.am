INCLUDES = $(PYTHON_INCLUDES) $(PYGTK_CFLAGS)
PYGTK_DEFSDIR = @PYGTK_DEFSDIR@

defs_DATA = 
defsdir = $(pkgdatadir)/2.0/defs

EXTRA_DIST = $(defs_DATA)

pkgpythondir = $(pythondir)/gst
pkgpyexecdir = $(pyexecdir)/gst

common_ldflags = -module -avoid-version

pygstdir = $(pkgpythondir)
pygst_PYTHON = __init__.py

pygstexecdir = $(pkgpyexecdir)

# GStreamer bindings
GST_OVERRIDES = gst.override gstpad-handlers.override
GST_DEFS = gst.defs gst-types.defs

pygstexec_LTLIBRARIES = _gst.la
_gst_la_CFLAGS = $(GST_CFLAGS) -fno-strict-aliasing
_gst_la_LIBADD = $(GST_LIBS)
_gst_la_LDFLAGS = $(common_ldflags) -export-symbols-regex init_gst
_gst_la_SOURCES = gst-argtypes.c gstmodule.c 
nodist__gst_la_SOURCES = gst.c
CLEANFILES = gst.c
EXTRA_DIST += $(GST_OVERRIDES) arg-types.py
defs_DATA += $(GST_DEFS)
gst.c: $(GST_DEFS) $(GST_OVERRIDES)

# GStreamer interfaces bindings

INTERFACES_OVERRIDES = interfaces.override xoverlay.override
INTERFACES_DEFS = interfaces.defs xoverlay.defs xwindowlistener.defs

GST_INTERFACES_INCLUDES=
GST_INTERFACES_INCLUDES += $(GST_INTERFACES_INCLUDEDIR)/gst/colorbalance/colorbalance.h
GST_INTERFACES_INCLUDES += $(GST_INTERFACES_INCLUDEDIR)/gst/colorbalance/colorbalancechannel.h
GST_INTERFACES_INCLUDES += $(GST_INTERFACES_INCLUDEDIR)/gst/mixer/mixer.h
GST_INTERFACES_INCLUDES += $(GST_INTERFACES_INCLUDEDIR)/gst/mixer/mixertrack.h
GST_INTERFACES_INCLUDES += $(GST_INTERFACES_INCLUDEDIR)/gst/navigation/navigation.h
GST_INTERFACES_INCLUDES += $(GST_INTERFACES_INCLUDEDIR)/gst/propertyprobe/propertyprobe.h
GST_INTERFACES_INCLUDES += $(GST_INTERFACES_INCLUDEDIR)/gst/tuner/tuner.h
GST_INTERFACES_INCLUDES += $(GST_INTERFACES_INCLUDEDIR)/gst/tuner/tunerchannel.h
GST_INTERFACES_INCLUDES += $(GST_INTERFACES_INCLUDEDIR)/gst/tuner/tunernorm.h

# FIXME: configure.ac
#if USE_X
GST_INTERFACES_INCLUDES+=$(GST_INTERFACES_INCLUDEDIR)/gst/xoverlay/xoverlay.h
GST_INTERFACES_INCLUDES+=$(GST_INTERFACES_INCLUDEDIR)/gst/xwindowlistener/xwindowlistener.h
#endif

# FIXEME: configure.ac
#if BUILD_INTERFACES
pygstexec_LTLIBRARIES += interfaces.la
#endif
interfaces_la_CFLAGS = $(GST_CFLAGS) $(GST_INTERFACES_CFLAGS) -fno-strict-aliasing
interfaces_la_LIBADD = $(GST_LIBS) $(GST_INTERFACES_LIBS)
interfaces_la_LDFLAGS = $(common_ldflags) -export-symbols-regex initinterface
interfaces_la_SOURCES = interfacesmodule.c
nodist_interfaces_la_SOURCES = interfaces.c
CLEANFILES += interfaces.c
EXTRA_DIST += $(INTERFACES_OVERRIDES) arg-types.py
defs_DATA += $(INTERFACES_DEFS)
interfaces.c: $(INTERFACES_DEFS) $(INTERFACES_OVERRIDES)

.defs.c:
	(cd $(srcdir) \
	&& $(PYGTK_CODEGEN) \
		--load-types $(srcdir)/arg-types.py \
		--register $(srcdir)/gst-types.defs \
		--override $*.override \
		--prefix py$* $*.defs) > gen-$*.c \
	&& cp gen-$*.c $*.c \
	&& rm -f gen-$*.c
