public_headers = \
		qa.h

c_sources = \
	gst-qa-runner.c \
	gst-qa-reporter.c \
	gst-qa-monitor.c \
	gst-qa-element-monitor.c \
	gst-qa-bin-monitor.c \
	gst-qa-pad-monitor.c \
	gst-qa-monitor-factory.c \
	gst-qa-report.c \
	gst-qa-scenario.c \
	gst-qa-override.c \
	gst-qa-override-registry.c \
	gst-qa-file-checker.c \
	gst-qa-monitor-preload.c

noinst_HEADERS =
	gst-qa-i18n-lib.h

lib_LTLIBRARIES = \
	libgstqa-@GST_API_VERSION@.la \
	libgstqa-default-overrides-@GST_API_VERSION@.la

libgstqa_@GST_API_VERSION@_la_SOURCES = \
    $(c_sources)

libgstqa_@GST_API_VERSION@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS)
libgstqa_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS) $(GIO_LDFLAGS)
libgstqa_@GST_API_VERSION@_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
	$(GST_LIBS) $(GIO_LIBS)

libgstqa_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/qa
libgstqa_@GST_API_VERSION@include_HEADERS = $(public_headers)

libgstqa_default_overrides_@GST_API_VERSION@_la_SOURCES = \
    gst-qa-default-overrides.c

libgstqa_default_overrides_@GST_API_VERSION@_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS)
libgstqa_default_overrides_@GST_API_VERSION@_la_LDFLAGS = $(GST_LIB_LDFLAGS) $(GST_ALL_LDFLAGS) \
	$(GST_LT_LDFLAGS) $(GIO_LDFLAGS)
libgstqa_default_overrides_@GST_API_VERSION@_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
	$(GST_LIBS) $(GIO_LIBS)

libgstqa_default_overrides_@GST_API_VERSION@includedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/qa
libgstqa_default_overrides_@GST_API_VERSION@include_HEADERS =

bin_PROGRAMS = gst-qa-@GST_API_VERSION@ gst-qa-transcoding-@GST_API_VERSION@
AM_CFLAGS =  -I$(top_srcdir) $(GST_PBUTILS_CFLAGS) $(GST_CFLAGS)
LDADD = $(top_builddir)/gst/qa/libgstqa-@GST_API_VERSION@.la $(GST_PBUTILS_LIBS) $(GST_LIBS)

gst-qa-transcoding-@GST_API_VERSION@$(EXEEXT): $(LTLIBRARIES)
gst-qa-@GST_API_VERSION@$(EXEEXT): $(LTLIBRARIES)

gst_qa_@GST_API_VERSION@_SOURCES = gst-qa.c
gst_qa_transcoding_@GST_API_VERSION@_SOURCES = gst-qa-transcoding.c
gst_qa_transcoding_@GST_API_VERSION@_CFLAG = $(GST_CFLAGS) $(GST_PBUTILS_CFLAGS)

CLEANFILES =
