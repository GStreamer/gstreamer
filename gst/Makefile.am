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

# what shall we build ?
if BUILD_INTERFACES
interface_lib = interfaces.la
else
interface_lib =
endif
if BUILD_PLAY
play_lib = play.la
else
play_lib =
endif

pygstexec_LTLIBRARIES = _gst.la $(interface_lib) $(play_lib)

# how shall we build them ?
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
PLAY_OVERRIDES = play.override
PLAY_DEFS = play.defs

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
