lib_LTLIBRARIES = libgstreamer-@GST_MAJORMINOR@.la
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

EXTRA_libgstreamer_@GST_MAJORMINOR@_la_SOURCES = gstcpuid_i386.s gstmarshal.list gstxml.c gsttypefind.c gstparse.c gstautoplug.c gsttrace.c

SUBDIRS = parse registries . $(GST_AUTOPLUG_DIRS) elements schedulers types indexers
DIST_SUBDIRS = autoplug elements parse registries schedulers types indexers

libgstreamer_@GST_MAJORMINOR@_la_SOURCES =		\
	gst.c			\
	gstmarshal.c		\
	gstenumtypes.c		\
	gstobject.c		\
	$(GST_AUTOPLUG_SRC)	\
	gstbin.c		\
	gstbuffer.c		\
	gstbufferpool-default.c	\
	gstcaps.c		\
	gstclock.c		\
	gstcpu.c		\
	gstdata.c		\
	gstelement.c		\
	gstelementfactory.c	\
	gstevent.c		\
	gstextratypes.c		\
	gstformat.c		\
	gstindex.c		\
	gstinfo.c		\
	gstmemchunk.c		\
	gstpad.c		\
	gstpipeline.c		\
	gstplugin.c		\
	gstpluginfeature.c	\
	gstprobe.c		\
	gstprops.c		\
	gstqueue.c		\
	gstscheduler.c		\
	gstsystemclock.c	\
	gstthread.c		\
	gstthreaddummy.c	\
	$(GST_TRACE_SRC)	\
	gsttype.c		\
	$(GST_TYPEFIND_SRC)	\
	gstutils.c		\
	gstregistry.c		\
	$(GST_PARSE_SRC)	\
	$(GSTARCH_SRCS)		\
	$(GST_LOADSAVE_SRC)


BUILT_SOURCES = gstmarshal.h gstmarshal.c gstenumtypes.h gstenumtypes.c

libgstreamer_@GST_MAJORMINOR@_la_CFLAGS = -D_GNU_SOURCE \
	$(LIBGST_CFLAGS) \
	-DG_LOG_DOMAIN=g_log_domain_gstreamer \
	-DGST_CACHE_DIR=\""$(GST_CACHE_DIR)"\" 
libgstreamer_@GST_MAJORMINOR@_la_LIBADD = $(LIBGST_LIBS) parse/libgstparse.la registries/libgstxmlregistry.la
libgstreamer_@GST_MAJORMINOR@_la_LDFLAGS = @GST_LT_LDFLAGS@ -version-info @GST_LIBVERSION@ 



libgstreamer_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst

gst_headers =			\
	gst.h			\
	gstatomic.h		\
	gstconfig.h		\
	gstobject.h		\
	gsttypes.h		\
	gstautoplug.h		\
	gstbin.h		\
	gstbuffer.h		\
	gstbufferpool-default.h \
	gstcaps.h		\
	gstclock.h		\
	gstcpu.h		\
	gstdata.h		\
	gstelement.h		\
	gstevent.h		\
	gstextratypes.h		\
	gstformat.h		\
	gstindex.h		\
	gstinfo.h		\
	gstlog.h		\
	gstmacros.h		\
	gstmemchunk.h		\
	gstpad.h		\
	gstpipeline.h		\
	gstplugin.h		\
	gstpluginfeature.h	\
	gstprobe.h		\
	gstprops.h		\
	gstqueue.h		\
	gstquery.h		\
	gstscheduler.h		\
	gstsystemclock.h	\
	gstthread.h		\
	gsttrace.h		\
	gsttrashstack.h		\
	gsttype.h		\
	gsttypefind.h		\
	gstutils.h		\
	gstregistry.h		\
	gstparse.h		\
	gstxml.h

built_headers =			\
	gstversion.h		\
	gstmarshal.h		\
	gstenumtypes.h

libgstreamer_@GST_MAJORMINOR@include_HEADERS = $(gst_headers) $(built_headers)

noinst_HEADERS = 		\
	gst_private.h		\
	gstdata_private.h	\
	gstarch.h		\
	cothreads.h		

# the compiler shoots cothreads.c in the head at -O6
libcothreads_la_SOURCES = cothreads.c
libcothreads_la_CFLAGS = $(libgstreamer_@GST_MAJORMINOR@_la_CFLAGS) 


gstmarshal.h: gstmarshal.list
	glib-genmarshal --header --prefix=gst_marshal $^ > gstmarshal.h.tmp
	mv gstmarshal.h.tmp gstmarshal.h

gstmarshal.c: gstmarshal.list
	echo "#include \"glib-object.h\"" >gstmarshal.c.tmp
	echo "#include \"gstlog.h\"" >> gstmarshal.c.tmp
	echo "#include \"gstmarshal.h\"" >> gstmarshal.c.tmp
	glib-genmarshal --body --prefix=gst_marshal $^ >> gstmarshal.c.tmp
	mv gstmarshal.c.tmp gstmarshal.c

gstenumtypes.h: $(gst_headers)
	glib-mkenums \
	--fhead "#ifndef __GST_ENUM_TYPES_H__\n#define __GST_ENUM_TYPES_H__\n\n#include <glib-object.h>\n\nG_BEGIN_DECLS\n" \
	--fprod "/* enumerations from \"@filename@\" */\n" \
	--vhead "GType @enum_name@_get_type (void);\n#define GST_TYPE_@ENUMSHORT@ (@enum_name@_get_type())\n"         \
	--ftail "G_END_DECLS\n\n#endif /* __GST_ENUM_TYPES_H__ */" \
	$^ > gstenumtypes.h

gstenumtypes.c: $(gst_headers)
	glib-mkenums \
	--fhead "#include <gst/gst.h>" \
	--fprod "\n/* enumerations from \"@filename@\" */" \
	--vhead "GType\n@enum_name@_get_type (void)\n{\n  static GType etype = 0;\n  if (etype == 0) {\n    static const G@Type@Value values[] = {"     \
	--vprod "      { @VALUENAME@, \"@VALUENAME@\", \"@valuenick@\" }," \
	--vtail "      { 0, NULL, NULL }\n    };\n    etype = g_@type@_register_static (\"@EnumName@\", values);\n  }\n  return etype;\n}\n" \
	$^ > gstenumtypes.c

# Don't want the generated marshal files in the dist
dist-hook:
	rm -f $(distdir)/gstmarshal.c $(distdir)/gstmarshal.h

# Clean generated files
distclean-local:
	rm -f $(top_builddir)/gst/gstmarshal.c $(top_builddir)/gst/gstmarshal.h

EXTRA_DIST = ROADMAP
