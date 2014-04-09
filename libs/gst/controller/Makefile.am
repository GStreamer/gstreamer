lib_LTLIBRARIES = libgstcontroller-@GST_API_VERSION@.la

libgstcontroller_@GST_API_VERSION@_includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/controller
libgstcontroller_@GST_API_VERSION@_include_HEADERS = \
	controller.h \
	gstargbcontrolbinding.h \
	gstdirectcontrolbinding.h \
	gsttimedvaluecontrolsource.h \
	gstinterpolationcontrolsource.h \
	gsttriggercontrolsource.h \
	gstlfocontrolsource.h

libgstcontroller_@GST_API_VERSION@_la_SOURCES = \
	gstargbcontrolbinding.c \
	gstdirectcontrolbinding.c \
	gsttimedvaluecontrolsource.c \
	gstinterpolationcontrolsource.c \
	gsttriggercontrolsource.c \
	gstlfocontrolsource.c

libgstcontroller_@GST_API_VERSION@_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstcontroller_@GST_API_VERSION@_la_LIBADD = $(GST_OBJ_LIBS) $(LIBM)
libgstcontroller_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) $(GST_LT_LDFLAGS)

CLEANFILES = *.gcno *.gcda *.gcov

%.c.gcov: .libs/libgstcontroller_@GST_API_VERSION@_la-%.gcda %.c
	$(GCOV) -b -f -o $^ > $@.out

gcov: $(libgstcontroller_@GST_API_VERSION@_la_SOURCES:=.gcov)

Android.mk: Makefile.am
	androgenizer -:PROJECT gstreamer -:SHARED libgstcontroller-@GST_API_VERSION@ -:TAGS eng debug \
	 -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstcontroller_@GST_API_VERSION@_la_SOURCES) \
	 -:CFLAGS $(libgstcontroller_@GST_API_VERSION@_la_CFLAGS) \
	 -:LDFLAGS $(libgstcontroller_@GST_API_VERSION@_la_LDFLAGS) \
	           $(libgstcontroller_@GST_API_VERSION@_la_LIBADD) \
	 -:HEADER_TARGET gstreamer-@GST_API_VERSION@/gst/controller \
	 -:HEADERS $(libgstcontroller_@GST_API_VERSION@_include_HEADERS) \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	> $@

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstController-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstcontroller_@GST_API_VERSION@_include_HEADERS))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstcontroller_@GST_API_VERSION@_la_SOURCES))

GstController-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstcontroller-@GST_API_VERSION@.la
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_UPDATE=no \
		$(INTROSPECTION_SCANNER) -v --namespace GstController \
		--nsversion=@GST_API_VERSION@ \
		--warn-all \
		--strip-prefix=Gst \
		-I$(top_srcdir) \
		-I$(top_srcdir)/libs \
		-I$(top_builddir) \
		-I$(top_builddir)/libs \
		--c-include "gst/controller/controller.h" \
		--add-include-path=$(top_builddir)/gst \
		--library-path=$(top_builddir)/gst \
		--library=libgstcontroller-@GST_API_VERSION@.la \
		--include=Gst-@GST_API_VERSION@ \
		--libtool="${LIBTOOL}" \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg-export gstreamer-controller-@GST_API_VERSION@ \
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
