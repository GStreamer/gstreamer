
lib_LTLIBRARIES = libgstreamer.la
noinst_LTLIBRARIES = libcothreads.la

if HAVE_CPU_I386
GSTARCH_SRCS = gstcpuid_i386.s
else
GSTARCH_SRCS = 
endif

#GST_INSTRUMENT_FLAGS = -finstrument-functions -DGST_ENABLE_FUNC_INSTRUMENTATION

if GST_DISABLE_LOADSAVE
GST_LOADSAVE_SRC = 
else
GST_LOADSAVE_SRC = gstxml.c
endif

if GST_DISABLE_TYPEFIND
GST_TYPEFIND_SRC = 
else
GST_TYPEFIND_SRC = gsttypefind.c
endif

if GST_DISABLE_PARSE
GST_PARSE_SRC = 
else
GST_PARSE_SRC = gstparse.c
endif

if GST_DISABLE_AUTOPLUG
GST_AUTOPLUG_SRC = 
GST_AUTOPLUG_DIRS =
else
GST_AUTOPLUG_SRC = gstautoplug.c
GST_AUTOPLUG_DIRS = autoplug
endif

if GST_DISABLE_TRACE
GST_TRACE_SRC = 
else
GST_TRACE_SRC = gsttrace.c
endif

EXTRA_libgstreamer_la_SOURCES = gstcpuid_i386.s gstmarshal.list gstxml.c gsttypefind.c gstparse.c gstautoplug.c gsttrace.c

SUBDIRS = parse . $(GST_AUTOPLUG_DIRS) elements schedulers types
DIST_SUBDIRS = autoplug elements parse types schedulers

libcothreads_la_SOURCES = cothreads.c
libgstreamer_la_SOURCES =		\
	gst.c			\
	gstmarshal.c		\
	gstobject.c		\
	$(GST_AUTOPLUG_SRC)	\
	gstbin.c		\
	gstbuffer.c		\
	gstbufferpool.c		\
	gstcaps.c		\
	gstclock.c		\
	gstcpu.c		\
	gstelement.c		\
	gstelementfactory.c	\
	gstevent.c		\
	gstextratypes.c		\
	gstinfo.c		\
	gstpad.c		\
	gstpipeline.c		\
	gstplugin.c		\
	gstpluginfeature.c	\
	gstprops.c		\
	gstqueue.c		\
	gstscheduler.c		\
	gstsystemclock.c	\
	gstthread.c		\
	$(GST_TRACE_SRC)	\
	gsttype.c		\
	$(GST_TYPEFIND_SRC)	\
	gstutils.c		\
	gsttimecache.c		\
	$(GST_PARSE_SRC)	\
	$(GSTARCH_SRCS)		\
	$(GST_LOADSAVE_SRC)


BUILT_SOURCES = gstmarshal.h gstmarshal.c

gstmarshal.h: gstmarshal.list
	glib-genmarshal --header --prefix=gst_marshal $(srcdir)/gstmarshal.list > gstmarshal.h.tmp
	mv gstmarshal.h.tmp gstmarshal.h

gstmarshal.c: gstmarshal.list
	echo "#include \"glib-object.h\"" >gstmarshal.c.tmp
	echo "#include \"gstlog.h\"" >> gstmarshal.c.tmp
	echo "#include \"gstmarshal.h\"" >> gstmarshal.c.tmp
	glib-genmarshal --body --prefix=gst_marshal $(srcdir)/gstmarshal.list >> gstmarshal.c.tmp
	mv gstmarshal.c.tmp gstmarshal.c

# Don't want the generated marshal files in the dist
dist-hook:
	rm -f $(distdir)/gstmarshal.c $(distdir)/gstmarshal.h
# Clean generated files
distclean-local:
	rm -f $(top_builddir)/gst/gstmarshal.c $(top_builddir)/gst/gstmarshal.h

libgstreamerincludedir = $(includedir)/gst
libgstreamerinclude_HEADERS =		\
	gst.h			\
	gstconfig.h		\
	gstmarshal.h		\
	gstobject.h		\
	gsttypes.h		\
	gstautoplug.h		\
	gstbin.h		\
	gstbuffer.h		\
	gstbufferpool.h		\
	gstcaps.h		\
	gstclock.h		\
	gstcpu.h		\
	gstdata.h		\
	gstelement.h		\
	gstevent.h		\
	gstextratypes.h		\
	gstinfo.h		\
	gstlog.h		\
	gstpad.h		\
	gstpipeline.h		\
	gstplugin.h		\
	gstpluginfeature.h	\
	gstprops.h		\
	gstqueue.h		\
	gstscheduler.h		\
	gstsystemclock.h	\
	gstthread.h		\
	gsttrace.h		\
	gsttype.h		\
	gsttypefind.h		\
	gstutils.h		\
	gsttimecache.h		\
	gstparse.h		\
	gstversion.h		\
	gstxml.h

noinst_HEADERS = 	\
	gst_private.h	\
	gstarch.h	\
	cothreads.h

libgstreamer_la_CFLAGS = -D_GNU_SOURCE -DGST_CONFIG_DIR=\""$(GST_CONFIG_DIR)"\" \
	$(LIBGST_CFLAGS) \
	-D_GNU_SOURCE \
	-DG_LOG_DOMAIN=g_log_domain_gstreamer \
	-DGST_CONFIG_DIR=\""$(GST_CONFIG_DIR)"\" 

# the compiler shoots cothreads.c in the head at -O6
libcothreads_la_CFLAGS = $(libgstreamer_la_CFLAGS) -O2 

libgstreamer_la_LIBADD = $(LIBGST_LIBS) parse/libgstparse.la
libgstreamer_la_LDFLAGS = @GST_LT_LDFLAGS@ -version-info @GST_LIBVERSION@ 

EXTRA_DIST = ROADMAP
