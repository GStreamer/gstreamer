built_header_make = gst-validate-enum-types.h
built_source_make = gst-validate-enum-types.c

source_c = \
	gst-validate-runner.c \
	gst-validate-reporter.c \
	gst-validate-mockdecryptor.c \
	gst-validate-monitor.c \
	gst-validate-element-monitor.c \
	gst-validate-bin-monitor.c \
	gst-validate-pipeline-monitor.c \
	gst-validate-pad-monitor.c \
	gst-validate-monitor-factory.c \
	gst-validate-report.c \
	gst-validate-scenario.c \
	gst-validate-override.c \
	gst-validate-utils.c \
	gst-validate-override-registry.c \
	media-descriptor.c \
	media-descriptor-writer.c \
	media-descriptor-parser.c \
	gst-validate-media-info.c \
    validate.c

source_h = \
	validate.h \
	validate-prelude.h \
	gst-validate-types.h \
	gst-validate-bin-monitor.h \
	gst-validate-pipeline-monitor.h \
	gst-validate-element-monitor.h \
	gst-validate-enums.h \
	media-descriptor.h \
	media-descriptor-writer.h \
	media-descriptor-parser.h \
	gst-validate-monitor-factory.h \
	gst-validate-monitor.h \
	gst-validate-override.h \
	gst-validate-override-registry.h \
	gst-validate-pad-monitor.h \
	gst-validate-reporter.h \
	gst-validate-report.h \
	gst-validate-runner.h \
	gst-validate-scenario.h \
	gst-validate-utils.h \
	gst-validate-media-info.h
#
# do not put files in the distribution that are generated
nodist_libgstvalidate_@GST_API_VERSION@_la_SOURCES = $(built_source_make)
nodist_libgstvalidate_@GST_API_VERSION@include_HEADERS = $(built_header_make)

gst-validate-enum-types.h: $(source_h)
	$(AM_V_GEN)$(GLIB_MKENUMS) \
	--template $(top_srcdir)/gst/validate/gst-validate-enum-types.h.template \
	$^ > gst-validate-enum-types.h

gst-validate-enum-types.c: $(source_h)
	$(AM_V_GEN)$(GLIB_MKENUMS) \
	--template $(top_srcdir)/gst/validate/gst-validate-enum-types.c.template \
	$^ > gst-validate-enum-types.c

EXTRA_DIST= \
	gst-validate-enum-types.c.template \
	gst-validate-enum-types.h.template

# BUILT_SOURCES are built on make all/check/install before all other targets
BUILT_SOURCES = \
        $(built_header_make)		\
        $(built_source_make)

noinst_HEADERS = 		\
	gettext.h \
	gst-validate-i18n-lib.h \
	gst-validate-mockdecryptor.h \
	gst-validate-internal.h

# GstValidate library
lib_LTLIBRARIES = libgstvalidate-@GST_API_VERSION@.la
libgstvalidate_@GST_API_VERSION@_la_SOURCES = $(source_c)
libgstvalidate_@GST_API_VERSION@include_HEADERS = $(source_h)
libgstvalidate_@GST_API_VERSION@_la_CFLAGS = $(GST_ALL_CFLAGS)\
	$(JSON_GLIB_CFLAGS) $(GIO_CFLAGS) $(GST_PBUTILS_CFLAGS) \
	-DGST_USE_UNSTABLE_API
libgstvalidate_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS) $(GIO_LDFLAGS) $(GST_PBUTILS_LDFAGS)
libgstvalidate_@GST_API_VERSION@_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
	$(GST_ALL_LIBS) $(GIO_LIBS) $(GST_PBUTILS_LIBS) \
	$(JSON_GLIB_LIBS) $(GLIB_LIBS) $(LIBM)

libgstvalidate_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/validate

# GstValidate GStreamer plugin
plugin_LTLIBRARIES = libgstvalidatetracer.la
nodist_libgstvalidatetracer_la_SOURCES = $(built_source_make)
libgstvalidatetracer_la_SOURCES = $(source_c)
libgstvalidatetracer_la_CFLAGS = $(GST_ALL_CFLAGS)\
	$(JSON_GLIB_CFLAGS) $(GIO_CFLAGS) $(GST_PBUTILS_CFLAGS) \
	-DGST_USE_UNSTABLE_API \
	-D__GST_VALIDATE_PLUGIN
libgstvalidatetracer_la_LDFLAGS = $(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS) $(GIO_LDFLAGS) $(GST_PBUTILS_LDFAGS) $(GST_PLUGIN_LDFLAGS)
libgstvalidatetracer_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
	$(GST_ALL_LIBS) $(GIO_LIBS) $(GST_PBUTILS_LIBS) \
	$(JSON_GLIB_LIBS) $(GLIB_LIBS) $(LIBM)

CLEANFILES = $(built_header_make) $(built_source_make) $(as_dll_cleanfiles) *.gcno *.gcda *.gcov *.gcov.out

if HAVE_INTROSPECTION
BUILT_GIRSOURCES = GstValidate-@GST_API_VERSION@.gir

gir_headers=$(patsubst %,$(srcdir)/%, $(libgstvalidate_@GST_API_VERSION@include_HEADERS))
gir_headers+=$(patsubst %,$(builddir)/%, $(built_header_make))
gir_sources=$(patsubst %,$(srcdir)/%, $(libgstvalidate_@GST_API_VERSION@_la_SOURCES))
gir_sources+=$(patsubst %,$(builddir)/%, $(built_source_make))
gir_cincludes=$(patsubst %,--c-include='gst/validate/%',$(libgstvalidate@GST_API_VERSION@include_HEADERS))

GstValidate-@GST_API_VERSION@.gir: $(INTROSPECTION_SCANNER) libgstvalidate-@GST_API_VERSION@.la
		$(AM_V_GEN)GST_PLUGIN_SYSTEM_PATH_1_0="" GST_PLUGIN_PATH_1_0="" GST_REGISTRY_DISABLE=yes GI_SCANNER_DISABLE_CACHE=yes \
		CPPFLAGS="$(CPPFLAGS)" CFLAGS="$(CFLAGS)" LDFLAGS="$(LDFLAGS)" CC="$(CC)" PKG_CONFIG="$(PKG_CONFIG)" DLLTOOL="$(DLLTOOL)" \
		$(INTROSPECTION_SCANNER) -v --namespace GstValidate \
		--nsversion=@GST_API_VERSION@ \
		--warn-all \
		$(gir_cincludes) \
		-I$(top_srcdir) \
		-I$(top_builddir) \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_API_VERSION@` \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-audio-@GST_API_VERSION@` \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-pbutils-@GST_API_VERSION@` \
		--add-include-path=`PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" $(PKG_CONFIG) --variable=girdir gstreamer-tag-@GST_API_VERSION@` \
		--library=libgstvalidate-@GST_API_VERSION@.la \
		--include=GLib-2.0 \
		--include=GstPbutils-@GST_API_VERSION@ \
		--include=GObject-2.0 \
		--include=GModule-2.0 \
		--include=GLib-2.0 \
		--libtool="${LIBTOOL}" \
		$(FAULTINJECTION_LIBS) \
		--pkg gstreamer-@GST_API_VERSION@ \
		--pkg gstreamer-pbutils-@GST_API_VERSION@ \
		--pkg gstreamer-controller-@GST_API_VERSION@ \
		--pkg glib-2.0 \
		--pkg gobject-2.0 \
		--pkg-export gstvalidate-@GST_API_VERSION@ \
		--add-init-section="$(INTROSPECTION_INIT)" \
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
	$(AM_V_GEN)PKG_CONFIG_PATH="$(GST_PKG_CONFIG_PATH)" \
		$(INTROSPECTION_COMPILER) \
		--includedir=$(srcdir) \
		--includedir=$(srcdir)/../video \
		--includedir=$(builddir) \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-@GST_API_VERSION@` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-pbutils-@GST_API_VERSION@` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-video-@GST_API_VERSION@` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-audio-@GST_API_VERSION@` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-tag-@GST_API_VERSION@` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-base-@GST_API_VERSION@` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gstreamer-controller-@GST_API_VERSION@` \
		--includedir=`$(PKG_CONFIG) --variable=girdir gio-2.0` \
		$(INTROSPECTION_COMPILER_OPTS) $< -o $(@F)
endif

CLEANFILES += $(BUILT_GIRSOURCES) $(typelibs_DATA)
