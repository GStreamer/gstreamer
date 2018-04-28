## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-gbm.la

libgstgl_gbm_la_SOURCES = \
	gstgl_gbm_utils.c \
	gstgldisplay_gbm.c \
	gstglwindow_gbm_egl.c

noinst_HEADERS = \
	gstgl_gbm_utils.h \
	gstgldisplay_gbm.h \
	gstglwindow_gbm_egl.h

libgstgl_gbm_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	-DBUILDING_GST_GL \
	$(GL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(DRM_CFLAGS) \
	$(GBM_CFLAGS) \
	$(G_UDEV_CFLAGS)

libgstgl_gbm_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS) \
	$(DRM_LIBS) \
	$(GBM_LIBS) \
	$(G_UDEV_LIBS)
