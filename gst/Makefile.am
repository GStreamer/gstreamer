INCLUDES = $(PYTHON_INCLUDES) $(PYGTK_CFLAGS)
PYGTK_DEFSDIR = @PYGTK_DEFSDIR@

defs_DATA = 
defsdir = $(pkgdatadir)/2.0/defs

EXTRA_DIST = $(defs_DATA) arg-types.py

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
EXTRA_DIST += $(GST_OVERRIDES)
defs_DATA += $(GST_DEFS)
gst.c: $(GST_DEFS) $(GST_OVERRIDES)

# gst-play bindings

# FIXEME: configure.ac
PLAY_OVERRIDES = play.override
PLAY_DEFS = play.defs

#if BUILD_PLAY
pygstexec_LTLIBRARIES += play.la
#endif
play_la_CFLAGS = $(GST_CFLAGS) $(GST_PLAY_CFLAGS) -fno-strict-aliasing
play_la_LIBADD = $(GST_LIBS) $(GST_PLAY_LIBS)
play_la_LDFLAGS = $(common_ldflags) -export-symbols-regex initplay
play_la_SOURCES = playmodule.c
nodist_play_la_SOURCES = play.c
CLEANFILES += play.c
EXTRA_DIST += $(PLAY_OVERRIDES)
defs_DATA += $(PLAY_DEFS)
play.c: $(PLAY_DEFS) $(PLAY_OVERRIDES)

# GStreamer interfaces bindings

INTERFACES_OVERRIDES = interfaces.override xoverlay.override
INTERFACES_DEFS = interfaces.defs xoverlay.defs xwindowlistener.defs

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
EXTRA_DIST += $(INTERFACES_OVERRIDES)
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
