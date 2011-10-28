lib_LTLIBRARIES = libgstbase-@GST_MAJORMINOR@.la

libgstbase_@GST_MAJORMINOR@_la_DEPENDENCIES = \
	$(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la
libgstbase_@GST_MAJORMINOR@_la_SOURCES = \
	gstadapter.c		\
	gstbaseparse.c		\
	gstbasesink.c		\
	gstbasesrc.c		\
	gstbasetransform.c	\
	gstbitreader.c		\
	gstbytereader.c		\
	gstbytewriter.c         \
	gstcollectpads.c	\
	gstcollectpads2.c	\
	gstpushsrc.c		\
	gsttypefindhelper.c	\
	gstdataqueue.c

libgstbase_@GST_MAJORMINOR@_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstbase_@GST_MAJORMINOR@_la_LIBADD = $(GST_OBJ_LIBS)
libgstbase_@GST_MAJORMINOR@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

libgstbase_@GST_MAJORMINOR@includedir =		\
	$(includedir)/gstreamer-@GST_MAJORMINOR@/gst/base

libgstbase_@GST_MAJORMINOR@include_HEADERS =	\
	gstadapter.h		\
	gstbaseparse.h		\
	gstbasesink.h		\
	gstbasesrc.h		\
	gstbasetransform.h	\
	gstbitreader.h		\
	gstbytereader.h		\
	gstbytewriter.h         \
	gstcollectpads.h	\
	gstcollectpads2.h	\
	gstpushsrc.h		\
	gsttypefindhelper.h	\
	gstdataqueue.h

noinst_HEADERS = \
	gstbytereader-docs.h \
	gstbytewriter-docs.h \
	gstbitreader-docs.h

CLEANFILES = *.gcno *.gcda *.gcov

%.c.gcov: .libs/libgstbase_@GST_MAJORMINOR@_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstbase_@GST_MAJORMINOR@_la_SOURCES:=.gcov)

Android.mk: Makefile.am
	androgenizer -:PROJECT gstreamer -:SHARED libgstbase-@GST_MAJORMINOR@ -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstbase_@GST_MAJORMINOR@_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(libgstbase_@GST_MAJORMINOR@_la_CFLAGS) \
	 -:LDFLAGS $(libgstbase_@GST_MAJORMINOR@_la_LDFLAGS) \
	           $(libgstbase_@GST_MAJORMINOR@_la_LIBADD) \
	 -:HEADER_TARGET gstreamer-@GST_MAJORMINOR@/gst/base \
	 -:HEADERS $(libgstbase_@GST_MAJORMINOR@include_HEADERS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstBase-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstbase_@GST_MAJORMINOR@include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstbase_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/base/%',$(libgstbase_@GST_MAJORMINOR@include_HEADERS))

GstBase-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstbase-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH="" GST_PLUGIN_PATH="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstBase \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		-I$(top_srcdir) \
		-I$(top_srcdir)/libs \
		-I$(top_builddir) \
		-I$(top_builddir)/libs \
		$(gir_cincludes) \
		--add-include-path=$(top_builddir)/gst \
		--library-path=$(top_builddir)/gst \
		--library=$(top_builddir)/gst/libgstreamer-0.10.la \
		--library=libgstbase-0.10.la \
		--include=Gst-0.10 \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-0.10 \
		--pkg-export gstreamer-base-0.10 \
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
