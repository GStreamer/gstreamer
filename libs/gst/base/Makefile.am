lib_LTLIBRARIES = libgstbase-@GST_API_VERSION@.la

libgstbase_@GST_API_VERSION@_la_DEPENDENCIES = \
	$(top_builddir)/gst/libgstreamer-@GST_API_VERSION@.la
libgstbase_@GST_API_VERSION@_la_SOURCES = \
	gstadapter.c		\
	gstbaseparse.c		\
	gstbasesink.c		\
	gstbasesrc.c		\
	gstbasetransform.c	\
	gstbitreader.c		\
	gstbytereader.c		\
	gstbytewriter.c         \
	gstcollectpads.c	\
	gstdataqueue.c		\
	gstflowcombiner.c	\
	gstpushsrc.c		\
	gstqueuearray.c		\
	gsttypefindhelper.c

libgstbase_@GST_API_VERSION@_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstbase_@GST_API_VERSION@_la_LIBADD = $(GST_OBJ_LIBS)
libgstbase_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

libgstbase_@GST_API_VERSION@includedir =		\
	$(includedir)/gstreamer-@GST_API_VERSION@/gst/base

libgstbase_@GST_API_VERSION@include_HEADERS =	\
	base.h			\
	gstadapter.h		\
	gstbaseparse.h		\
	gstbasesink.h		\
	gstbasesrc.h		\
	gstbasetransform.h	\
	gstbitreader.h		\
	gstbytereader.h		\
	gstbytewriter.h         \
	gstcollectpads.h	\
	gstdataqueue.h		\
	gstflowcombiner.h	\
	gstpushsrc.h		\
	gstqueuearray.h		\
	gsttypefindhelper.h

noinst_HEADERS = \
	gstbytereader-docs.h \
	gstbytewriter-docs.h \
	gstbitreader-docs.h \
	gstindex.h

EXTRA_DIST = gstindex.c gstmemindex.c

CLEANFILES = *.gcno *.gcda *.gcov

%.c.gcov: .libs/libgstbase_@GST_API_VERSION@_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstbase_@GST_API_VERSION@_la_SOURCES:=.gcov)

Android.mk: Makefile.am
	androgenizer -:PROJECT gstreamer -:SHARED libgstbase-@GST_API_VERSION@ -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstbase_@GST_API_VERSION@_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(libgstbase_@GST_API_VERSION@_la_CFLAGS) \
	 -:LDFLAGS $(libgstbase_@GST_API_VERSION@_la_LDFLAGS) \
	           $(libgstbase_@GST_API_VERSION@_la_LIBADD) \
	 -:HEADER_TARGET gstreamer-@GST_API_VERSION@/gst/base \
	 -:HEADERS $(libgstbase_@GST_API_VERSION@include_HEADERS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstBase-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstbase_@GST_API_VERSION@include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstbase_@GST_API_VERSION@_la_SOURCES))

GstBase-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstbase-@GST_API_VERSION@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstBase \
		--nsversion=@GST_API_VERSION@ \
		--warn-all \
		--strip-prefix=Gst \
		-I$(top_srcdir) \
		-I$(top_srcdir)/libs \
		-I$(top_builddir) \
		-I$(top_builddir)/libs \
		--c-include "gst/base/base.h" \
		--add-include-path=$(top_builddir)/gst \
		--library-path=$(top_builddir)/gst \
		--library=libgstbase-@GST_API_VERSION@.la \
		--include=Gst-@GST_API_VERSION@ \
		--libtool="${LIBTOOL}" \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg-export gstreamer-base-@GST_API_VERSION@ \
		--add-init-section="gst_init(NULL,NULL);" \
		--output $@ \
		$(gir_headers) \
		$(gir_sources)

# INTROSPECTION_GIRDIR/INTROSPECTION_TYPELIBDIR aren't the right place to
# install anything - we need to install inside our prefix.
girdir = $(datadir)/gir-1.0
gir_DATA = $(BUILT_GIRSOURCES)

typelibsdir = $(libdir)/girepository-1.0/

typelibs_DATA = $(BUILT_GIRSOURCES:.gir=.typelib)

%.typelib: %.gir $(INTROSPECTION_COMPILER)
	$(AM_V_GEN)$(INTROSPECTION_COMPILER) --includedir=$(srcdir) --includedir=$(builddir) --includedir=$(top_builddir)/gst $(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
endif
