bin_PROGRAMS = \
	gst-validate-@GST_API_VERSION@ \
	gst-validate-transcoding-@GST_API_VERSION@ \
	gst-validate-media-check-@GST_API_VERSION@

bin_SCRIPTS = \
	gst-validate-launcher

AM_CFLAGS = $(GST_ALL_CFLAGS) $(GST_PBUTILS_CFLAGS)  $(GST_VIDEO_CFLAGS)
LDADD = $(top_builddir)/gst/validate/libgstvalidate-@GST_API_VERSION@.la $(GST_PBUTILS_LIBS) $(GST_LIBS)  $(GST_VIDEO_LIBS) $(FAULTINJECTION_LIBS)

gst_validate_@GST_API_VERSION@_SOURCES = gst-validate.c
gst_validate_@GST_API_VERSION@_CFLAGS = $(GIO_CFLAGS) $(AM_CFLAGS)
gst_validate_@GST_API_VERSION@_LDADD = $(GIO_LIBS) $(LDADD)
gst_validate_transcoding_@GST_API_VERSION@_SOURCES = gst-validate-transcoding.c
gst_validate_media_check_@GST_API_VERSION@_SOURCES = gst-validate-media-check.c

CLEANFILES = $(bin_SCRIPTS)
