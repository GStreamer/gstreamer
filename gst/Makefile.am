INCLUDES = $(PYTHON_INCLUDES) $(PYGTK_CFLAGS)
PYGTK_DEFSDIR = @PYGTK_DEFSDIR@

pygstreamerdir = $(pythondir)/gstreamer
pygstreamer_PYTHON = __init__.py

pygstreamerexecdir = $(pyexecdir)/gstreamer

# gstreamer binding

pyexec_LTLIBRARIES = _gstreamermodule.la
_gstreamermodule_la_SOURCES = gstreamermodule.c gstreamer-fixes.c gstreamer-fixes.h
_gstreamermodule_la_CFLAGS = $(GST_CFLAGS)
_gstreamermodule_la_LIBADD = $(GST_LIBS)
_gstreamermodule_la_LDFLAGS = -module -avoid-version -export-symbols-regex initgstreamer
nodist__gstreamermodule_la_SOURCES = gstreamer.c
CLEANFILES = gstreamer.c gstreamer-base.defs gstreamer.defs
EXTRA_DIST = gstreamer-extra.defs gstreamer.override gstreamer-arg-types.py
gstreamer.c: gstreamer.defs gstreamer.override

GST_EXCLUDE_INCLUDES=\
	$(GST_INCLUDEDIR)/gst/gstatomic_impl.h \
	$(GST_INCLUDEDIR)/gst/gstcompat.h
GST_INCLUDES=$(filter-out $(GST_EXCLUDE_INCLUDES),$(wildcard $(GST_INCLUDEDIR)/gst/*.h))

gstreamer-base.defs: $(GST_INCLUDES)
	$(PYTHON) $(PYGTK_H2DEF) $(GST_INCLUDES) > gstreamer-base.defs

gstreamer.defs: gstreamer-base.defs gstreamer-extra.defs $(top_srcdir)/gstreamer/gstreamer-arg-types.py
	cat gstreamer-base.defs $(srcdir)/gstreamer-extra.defs > gstreamer.defs

.defs.c:
	$(PYGTK_CODEGEN) \
		--load-types $(srcdir)/gstreamer-arg-types.py \
		--override $(srcdir)/$*.override \
		--prefix py$* $*.defs > gen-$*.c \
	&& cp gen-$*.c $*.c \
	&& rm -f gen-$*.c

#	    --register $(PYGTK_DEFSDIR)/pango-types.defs \
#	    --register $(PYGTK_DEFSDIR)/gdk-types.defs \
#	    --register $(PYGTK_DEFSDIR)/gtk-types.defs \
#	    --register $(top_srcdir)/bonobo/bonobo-types.defs \
#	    --register $(top_srcdir)/bonobo/bonoboui-types.defs \
