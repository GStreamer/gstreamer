MODULE = gstreamer

INCLUDES = $(PYTHON_INCLUDES) $(PYGTK_CFLAGS)
PYGTK_DEFSDIR = @PYGTK_DEFSDIR@

pygstreamerdir = $(pythondir)/$(MODULE)
pygstreamer_PYTHON = __init__.py

pygstreamerexecdir = $(pyexecdir)/$(MODULE)

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

pyexec_LTLIBRARIES = _gstreamermodule.la
_gstreamermodule_la_SOURCES = \
	gstreamermodule.c \
	common.c common.h \
	$(VERSOURCES)
_gstreamermodule_la_CFLAGS = $(GST_CFLAGS) -fno-strict-aliasing
_gstreamermodule_la_LIBADD = $(GST_LIBS)
_gstreamermodule_la_LDFLAGS = -module -avoid-version -export-symbols-regex init_gstreamer
nodist__gstreamermodule_la_SOURCES = gstreamer.c

CLEANFILES = $(MODULE).c h2def.defs $(MODULE).defs $(MODULE).override
EXTRA_DIST = $(GST_OVERRIDES) $(GST_DEFS) $(GST_CODE) arg-types.py

GST_EXCLUDE_INCLUDES=\
	$(GST_INCLUDEDIR)/gst/gstatomic_impl.h \
	$(GST_INCLUDEDIR)/gst/gstcompat.h
GST_INCLUDES=$(filter-out $(GST_EXCLUDE_INCLUDES),$(wildcard $(GST_INCLUDEDIR)/gst/*.h))

gstreamer.override: common.override $(GST_MAJORMINOR).override
	cat $+ > $@

h2def.defs: $(GST_INCLUDES)
	$(PYTHON) $(PYGTK_H2DEF) $(GST_INCLUDES) > $@

gstreamer.defs: h2def.defs common.defs $(GST_MAJORMINOR).defs
	cat $+ > $@

gstreamer.c: $(MODULE).defs arg-types.py $(MODULE).override
	$(PYGTK_CODEGEN) \
		--load-types $(srcdir)/arg-types.py \
		--override $(srcdir)/$(MODULE).override \
		--prefix py$(MODULE) $(MODULE).defs > gen-$(MODULE).c \
	&& cp gen-$(MODULE).c $(MODULE).c \
	&& rm -f gen-$(MODULE).c

#		--register $(PYGTK_DEFSDIR)/gtk-types.defs
#		--register $(top_srcdir)/blah/blah-types.defs
