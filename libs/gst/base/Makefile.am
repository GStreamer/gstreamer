lib_LTLIBRARIES = libgstbase-@GST_MAJORMINOR@.la

libgstbase_@GST_MAJORMINOR@_la_DEPENDENCIES = \
	$(top_builddir)/gst/libgstreamer-@GST_MAJORMINOR@.la
libgstbase_@GST_MAJORMINOR@_la_SOURCES = \
	gstadapter.c		\
	gstbasesink.c		\
	gstbasesrc.c		\
	gstbasetransform.c	\
	gstbitreader.c		\
	gstbytereader.c		\
	gstbytewriter.c         \
	gstcollectpads.c	\
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
	gstbasesink.h		\
	gstbasesrc.h		\
	gstbasetransform.h	\
	gstbitreader.h		\
	gstbytereader.h		\
	gstbytewriter.h         \
	gstcollectpads.h	\
	gstpushsrc.h		\
	gsttypefindhelper.h	\
	gstdataqueue.h

noinst_HEADERS = \
	gstbytereader-docs.h

CLEANFILES = *.gcno *.gcda *.gcov

%.c.gcov: .libs/libgstbase_@GST_MAJORMINOR@_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstbase_@GST_MAJORMINOR@_la_SOURCES:=.gcov)

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstBase-@GST_MAJORMINOR@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstbase_@GST_MAJORMINOR@include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstbase_@GST_MAJORMINOR@_la_SOURCES))
gir_cincludes=$(patsubst %,--c-include='gst/base/%',$(libgstbase_@GST_MAJORMINOR@include_HEADERS))

GstBase-@GST_MAJORMINOR@.gir: $(INTROSPECTION_SCANNER) libgstbase-@GST_MAJORMINOR@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(top_builddir)/pkgconfig:$(PKG_CONFIG_PATH)" \
		$(INTROSPECTION_SCANNER) -v --namespace GstBase \
		--nsversion=@GST_MAJORMINOR@ \
		--strip-prefix=Gst \
		-I$(top_srcdir) \
		$(gir_cincludes) \
		--add-include-path=$(top_builddir)/gst \
		--library=libgstbase-0.10.la \
		--include=Gst-0.10 \
		--libtool="$(top_builddir)/libtool" \
		--pkg gstreamer-0.10 \
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
