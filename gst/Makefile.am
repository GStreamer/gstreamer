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

GST_OVERRIDES = gst.override gstpad-handlers.override

pygstexec_LTLIBRARIES = _gstmodule.la
_gstmodule_la_SOURCES = gstmodule.c gst-types.c

_gstmodule_la_CFLAGS = $(GST_CFLAGS) -fno-strict-aliasing
_gstmodule_la_LIBADD = $(GST_LIBS)
_gstmodule_la_LDFLAGS = -module -avoid-version -export-symbols-regex init_gst
nodist__gstmodule_la_SOURCES = gst.c

CLEANFILES = gst.c
EXTRA_DIST = gst.defs gst-types.defs $(GST_OVERRIDES) arg-types.py

gst.c: $(srcdir)/gst.defs $(srcdir)/arg-types.py $(GST_OVERRIDES)
	$(PYGTK_CODEGEN) \
		--load-types $(srcdir)/arg-types.py \
	        --register $(srcdir)/gst-types.defs \
		--override $(srcdir)/gst.override \
		--prefix pygst $(srcdir)/gst.defs > gen-gst.c \
        && rm -fr gtreamer.c \
        && echo '/* GENERATED FILE - DO NOT EDIT */' >> gst.c \
	&& cat gen-gst.c >> gst.c \
	&& rm -f gen-gst.c
