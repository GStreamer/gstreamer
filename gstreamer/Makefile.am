INCLUDES = $(PYTHON_INCLUDES) $(PYGTK_CFLAGS)
PYGTK_DEFSDIR = @PYGTK_DEFSDIR@

pkgpythondir = $(pythondir)/gst
pkgpyexecdir = $(pyexecdir)/gst

pygstdir = $(pkgpythondir)
pygst_PYTHON = __init__.py

# Backwards comptibility
pygstreamerdir = $(pythondir)
pygstreamer_PYTHON = gstreamer.py

pygstexecdir = $(pkgpyexecdir)

GST_OVERRIDES = \
	gstreamer.override		\
	gstpad-handlers.override

pygstexec_LTLIBRARIES = _gstmodule.la
_gstmodule_la_SOURCES = \
	gstreamermodule.c 		\
	gst-types.c 			\
	$(VERSOURCES)

_gstmodule_la_CFLAGS = $(GST_CFLAGS) -fno-strict-aliasing
_gstmodule_la_LIBADD = $(GST_LIBS)
_gstmodule_la_LDFLAGS = -module -avoid-version -export-symbols-regex init_gst
nodist__gstmodule_la_SOURCES = gstreamer.c

CLEANFILES = gstreamer.c
EXTRA_DIST = gstreamer.defs $(GST_OVERRIDES) arg-types.py

gstreamer.c: $(srcdir)/gstreamer.defs $(srcdir)/arg-types.py $(GST_OVERRIDES)
	$(PYGTK_CODEGEN) \
		--load-types $(srcdir)/arg-types.py \
	        --register $(srcdir)/gst-types.defs \
		--override $(srcdir)/gstreamer.override \
		--prefix pygst gstreamer.defs > gen-gstreamer.c \
        && rm -fr gtreamer.c \
        && echo '/* GENERATED FILE - DO NOT EDIT */' >> gstreamer.c \
	&& cat gen-gstreamer.c >> gstreamer.c \
	&& rm -f gen-gstreamer.c
