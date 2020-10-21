bin_PROGRAMS = \
	gst-validate-@GST_API_VERSION@ \
	gst-validate-transcoding-@GST_API_VERSION@ \
	gst-validate-media-check-@GST_API_VERSION@

noinst_PROGRAMS = \
	gst-validate-@GST_API_VERSION@-debug \
	gst-validate-transcoding-@GST_API_VERSION@-debug \
	gst-validate-media-check-@GST_API_VERSION@-debug

bin_SCRIPTS = \
	gst-validate-launcher

AM_CFLAGS = $(GST_ALL_CFLAGS) $(GST_PBUTILS_CFLAGS)  $(GST_VIDEO_CFLAGS)
LDADD = $(top_builddir)/gst/validate/libgstvalidate-@GST_API_VERSION@.la $(GST_PLUGINS_BASE_LIBS) -lgstapp-1.0 $(GST_PBUTILS_LIBS) $(GST_VIDEO_LIBS) $(GST_LIBS)

# The -debug versions are used when running from sources to not confuse
# valgrind with libtool's wrappers. Those are built with the '-no-install' and
# so use rpath instead of wrappers.
gst_validate_@GST_API_VERSION@_SOURCES = gst-validate.c
gst_validate_@GST_API_VERSION@_CFLAGS = $(GIO_CFLAGS) $(AM_CFLAGS)
gst_validate_@GST_API_VERSION@_LDADD = $(GIO_LIBS) $(LDADD)

gst_validate_@GST_API_VERSION@_debug_SOURCES = gst-validate.c
gst_validate_@GST_API_VERSION@_debug_CFLAGS = $(GIO_CFLAGS) $(AM_CFLAGS)
gst_validate_@GST_API_VERSION@_debug_LDADD = $(GIO_LIBS) $(LDADD)
gst_validate_@GST_API_VERSION@_debug_LDFLAGS = -no-install

gst_validate_transcoding_@GST_API_VERSION@_SOURCES = gst-validate-transcoding.c
gst_validate_transcoding_@GST_API_VERSION@_debug_SOURCES = gst-validate-transcoding.c
gst_validate_transcoding_@GST_API_VERSION@_debug_LDFLAGS = -no-install

gst_validate_media_check_@GST_API_VERSION@_SOURCES = gst-validate-media-check.c
gst_validate_media_check_@GST_API_VERSION@_debug_SOURCES = gst-validate-media-check.c
gst_validate_media_check_@GST_API_VERSION@_debug_LDFLAGS = -no-install

if HAVE_GST_RTSP_SERVER
bin_PROGRAMS += gst-validate-rtsp-server-@GST_API_VERSION@
noinst_PROGRAMS += gst-validate-rtsp-server-@GST_API_VERSION@-debug

gst_validate_rtsp_server_@GST_API_VERSION@_SOURCES = gst-validate-rtsp-server.c
gst_validate_rtsp_server_@GST_API_VERSION@_CFLAGS = $(GST_RTSP_SERVER_CFLAGS)
gst_validate_rtsp_server_@GST_API_VERSION@_LDADD = $(LDADD) $(GST_RTSP_SERVER_LIBS)
gst_validate_rtsp_server_@GST_API_VERSION@_debug_SOURCES = gst-validate-rtsp-server.c
gst_validate_rtsp_server_@GST_API_VERSION@_debug_LDADD = $(LDADD) $(GST_RTSP_SERVER_LIBS)
gst_validate_rtsp_server_@GST_API_VERSION@_debug_LDFLAGS = -no-install
gst_validate_rtsp_server_@GST_API_VERSION@_debug_CFLAGS = $(GST_RTSP_SERVER_CFLAGS)
endif

if HAVE_CAIRO
bin_PROGRAMS += gst-validate-images-check-@GST_API_VERSION@
noinst_PROGRAMS += gst-validate-images-check-@GST_API_VERSION@-debug

gst_validate_images_check_@GST_API_VERSION@_SOURCES = gst-validate-images-check.c
gst_validate_images_check_@GST_API_VERSION@_LDADD = $(top_builddir)/gst-libs/gst/video/libgstvalidatevideo-@GST_API_VERSION@.la $(LDADD)
gst_validate_images_check_@GST_API_VERSION@_debug_SOURCES = gst-validate-images-check.c
gst_validate_images_check_@GST_API_VERSION@_debug_LDADD = $(top_builddir)/gst-libs/gst/video/libgstvalidatevideo-@GST_API_VERSION@.la $(LDADD)
gst_validate_images_check_@GST_API_VERSION@_debug_LDFLAGS = -no-install
endif

CLEANFILES = $(bin_SCRIPTS)
