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

if GST_DISABLE_PARSE
GST_PARSE_SRC = 
GST_PARSE_DIRS = 
GST_PARSE_LIBADD = 
else
GST_PARSE_SRC = gstparse.c
GST_PARSE_DIRS = parse
GST_PARSE_LIBADD = parse/libgstparse.la 
endif

if GST_DISABLE_TRACE
GST_TRACE_SRC = 
else
GST_TRACE_SRC = gsttrace.c
endif

if GST_DISABLE_REGISTRY
GST_REGISTRY_SRC = 
GST_REGISTRY_DIRS =
GST_REGISTRY_LIBADD = 
else
GST_REGISTRY_SRC = gstregistry.c
GST_REGISTRY_DIRS = registries
GST_REGISTRY_LIBADD = registries/libgstxmlregistry.la
endif

if GST_DISABLE_ENUMTYPES
GST_ENUMTYPES_SRC = 
else
GST_ENUMTYPES_SRC = gstenumtypes.c
endif

if GST_DISABLE_INDEX
GST_INDEX_SRC = 
GST_INDEX_DIRS =
else
GST_INDEX_SRC = gstindex.c
GST_INDEX_DIRS = indexers
endif

if GST_DISABLE_PLUGIN
GST_PLUGIN_SRC = 
else
GST_PLUGIN_SRC = gstplugin.c
endif

if GST_DISABLE_URI
GST_URI_SRC = 
else
GST_URI_SRC = gsturi.c
endif

EXTRA_libgstreamer_@GST_MAJORMINOR@_la_SOURCES = gstcpuid_i386.s gstmarshal.list gstxml.c gstparse.c gsttrace.c

SUBDIRS = $(GST_PARSE_DIRS) $(GST_REGISTRY_DIRS) . autoplug elements schedulers $(GST_INDEX_DIRS) 
DIST_SUBDIRS = autoplug elements parse registries schedulers indexers

libgstreamer_@GST_MAJORMINOR@_la_SOURCES =		\
	gst.c			\
	$(GST_ENUMTYPES_SRC)	\
	gstobject.c		\
	gstatomic.c		\
	gstbin.c		\
	gstbuffer.c		\
	gstcaps.c		\
	gstclock.c		\
	gstcpu.c		\
	gstdata.c		\
	gstelement.c		\
	gstelementfactory.c	\
	gsterror.c		\
	gstevent.c		\
	gstfilter.c		\
	gstformat.c		\
	$(GST_INDEX_SRC)	\
	gstinfo.c		\
	gstinterface.c		\
	gstmemchunk.c		\
	gstpad.c		\
	gstpipeline.c		\
	gstplugin.c		\
	gstpluginfeature.c	\
	gstprobe.c		\
	gstqueue.c		\
	gstquery.c		\
	gstscheduler.c		\
	gststructure.c		\
	gstsystemclock.c	\
	gsttag.c		\
	gsttaginterface.c	\
	gstthread.c		\
	gstthreaddummy.c	\
	$(GST_TRACE_SRC)	\
	gsttrashstack.c		\
	gsttypefind.c		\
	$(GST_URI_SRC)		\
	gsturitype.c		\
	gstutils.c		\
	gstvalue.c		\
	$(GST_REGISTRY_SRC)	\
	gstregistrypool.c	\
	$(GST_PARSE_SRC)	\
	$(GSTARCH_SRCS)		\
	$(GST_LOADSAVE_SRC)

nodist_libgstreamer_@GST_MAJORMINOR@_la_SOURCES =		\
	gstmarshal.c

BUILT_SOURCES = gstversion.h gstconfig.h gstmarshal.h gstmarshal.c gstenumtypes.h $(GST_ENUMTYPES_SRC) 
CLEANFILES = gstmarshal.h gstmarshal.c gstenumtypes.h $(GST_ENUMTYPES_SRC)
DISTCLEANFILES = gstversion.h gstconfig.h

libgstreamer_@GST_MAJORMINOR@_la_CFLAGS = -D_GNU_SOURCE \
	$(GST_CFLAGS) \
	-DG_LOG_DOMAIN=g_log_domain_gstreamer \
	-DGST_MAJORMINOR=\""$(GST_MAJORMINOR)"\" 
libgstreamer_@GST_MAJORMINOR@_la_LIBADD = $(LIBGST_LIBS) $(GST_PARSE_LIBADD) $(GST_REGISTRY_LIBADD)
libgstreamer_@GST_MAJORMINOR@_la_LDFLAGS = @GST_LT_LDFLAGS@ -version-info @GST_LIBVERSION@ 


libgstreamer_@GST_MAJORMINOR@includedir = $(includedir)/gstreamer-@GST_MAJORMINOR@/gst

gst_headers =			\
	gst.h			\
	gstatomic.h		\
	gstobject.h		\
	gstbin.h		\
	gstbuffer.h		\
	gstcaps.h		\
	gstclock.h		\
	gstcompat.h		\
	gstcpu.h		\
	gstdata.h		\
	gstelement.h		\
	gsterror.h		\
	gstevent.h		\
	gstfilter.h		\
	gstformat.h		\
	gstindex.h		\
	gstinfo.h		\
	gstinterface.h		\
	gstlog.h		\
	gstmacros.h		\
	gstmemchunk.h		\
	gstpad.h		\
	gstpipeline.h		\
	gstplugin.h		\
	gstpluginfeature.h	\
	gstprobe.h		\
	gstqueue.h		\
	gstquery.h		\
	gstscheduler.h		\
	gststructure.h		\
	gstsystemclock.h	\
	gsttag.h		\
	gsttaginterface.h	\
	gstthread.h		\
	gsttrace.h		\
	gsttrashstack.h		\
	gsttypefind.h		\
	gsttypes.h		\
	gsturi.h		\
	gsturitype.h		\
	gstutils.h		\
	gstvalue.h		\
	gstregistry.h		\
	gstregistrypool.h	\
	gstparse.h		\
	gstxml.h

libgstreamer_@GST_MAJORMINOR@include_HEADERS = $(gst_headers)
nodist_libgstreamer_@GST_MAJORMINOR@include_HEADERS = \
	gstconfig.h		\
	gstversion.h		\
	gstmarshal.h		\
	gstenumtypes.h

noinst_HEADERS = 		\
	gettext.h		\
	gst-i18n-lib.h		\
	gst-i18n-app.h		\
	gst_private.h		\
	gstatomic_impl.h	\
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

EXTRA_DIST = ROADMAP
