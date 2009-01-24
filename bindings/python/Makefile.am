SUBDIRS = codegen
pkgpyexecdir = $(pyexecdir)/gst-$(GST_MAJORMINOR)/gst

# we install everything in pyexecdir; otherwise you end up with a mess for
# multilib
pygstrtspserverdir = $(pkgpyexecdir)
pygstrtspserver_PYTHON =

pygstrtspserverexecdir = $(pkgpyexecdir)
pygstrtspserverexec_LTLIBRARIES = rtspserver.la

DEFS = rtspserver-types.defs rtspserver.defs
defs_DATA = $(DEFS)
defsdir = $(pkgdatadir)/$(GST_MAJORMINOR)/defs
OVERRIDES = rtspserver.override

INCLUDES = -I$(top_srcdir) -I$(srcdir) $(PYTHON_INCLUDES)

rtspserver_la_CFLAGS = -I$(top_srcdir)/src \
		$(PYGOBJECT_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
rtspserver_la_LDFLAGS = -export-symbols-regex "^(initrtspserver|_PyGObject_API).*" \
	-module -avoid-version $(GST_PLUGIN_LDFLAGS)
rtspserver_la_LIBADD = $(top_builddir)/gst/rtsp-server/libgstrtspserver-@GST_MAJORMINOR@.la \
		$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
		-lgstrtp-@GST_MAJORMINOR@ -lgstrtsp-@GST_MAJORMINOR@ \
        	-lgstsdp-@GST_MAJORMINOR@ $(GST_LIBS) $(LIBM)
rtspserver_la_SOURCES = rtspservermodule.c 
nodist_rtspserver_la_SOURCES = rtspserver.c

EXTRA_DIST = $(defs_DATA) $(OVERRIDES) arg-types.py

CLEANFILES = rtspserver.c

rtspserver.c: $(DEFS) $(OVERRIDES) arg-types.py

.defs.c: 
	($(PYTHON) $(srcdir)/codegen/codegen.py \
		--load-types $(srcdir)/arg-types.py \
		--register $(srcdir)/rtspserver-types.defs \
		--override $(srcdir)/$*.override \
		--extendpath $(top_builddir)/gst/ \
		--extendpath $(srcdir)/ \
		--prefix pygst_rtsp_server $<) > gen-$*.c \
	&& cp gen-$*.c $*.c \
	&& rm -f gen-$*.c
