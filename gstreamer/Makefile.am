MODULE = gstreamer

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

GST_OVERRIDES = common.override \
	0.6.override \
	0.7.override
GST_DEFS = common.defs \
	0.6.defs \
	0.7.defs
GST_CODE = common.c common.h \
	0.6.c 0.6.h \
	0.7.c 0.7.h

# Ugly hack to pick the proper version code.
# Just setting to $(GST_MAJORMINOR).{ch} will not work
if GST_0_6
VERSOURCES = 0.6.c 0.6.h
endif
if GST_0_7
VERSOURCES = 0.7.c 0.7.h
endif

pygstexec_LTLIBRARIES = _gstmodule.la
_gstmodule_la_SOURCES = \
	gstreamermodule.c \
	common.c common.h \
	$(VERSOURCES)
_gstmodule_la_CFLAGS = $(GST_CFLAGS) -fno-strict-aliasing
_gstmodule_la_LIBADD = $(GST_LIBS)
_gstmodule_la_LDFLAGS = -module -avoid-version -export-symbols-regex init_gst
nodist__gstmodule_la_SOURCES = $(MODULE).c

CLEANFILES = $(MODULE).c
EXTRA_DIST = $(MODULE).defs $(GST_OVERRIDES) $(GST_DEFS) $(GST_CODE) arg-types.py

GST_EXCLUDE_INCLUDES=\
	$(GST_INCLUDEDIR)/gst/gstatomic_impl.h \
	$(GST_INCLUDEDIR)/gst/gstcompat.h
GST_INCLUDES=$(filter-out $(GST_EXCLUDE_INCLUDES),$(wildcard $(GST_INCLUDEDIR)/gst/*.h))

gstreamer.c: $(srcdir)/$(MODULE).defs $(srcdir)/arg-types.py $(srcdir)/$(MODULE).override
	$(PYGTK_CODEGEN) \
		--load-types $(srcdir)/arg-types.py \
		--override $(srcdir)/$(MODULE).override \
		--prefix py$(MODULE) $(MODULE).defs > gen-$(MODULE).c \
	&& cp gen-$(MODULE).c $(MODULE).c \
	&& rm -f gen-$(MODULE).c

#		--register $(PYGTK_DEFSDIR)/gtk-types.defs
#		--register $(top_srcdir)/blah/blah-types.defs
