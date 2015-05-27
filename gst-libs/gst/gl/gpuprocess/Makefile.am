## Process this file with automake to produce Makefile.in

noinst_LTLIBRARIES = libgstgl-gpuprocess.la

libgstgl_gpuprocess_la_SOURCES = \
	gstglcontext_gpu_process.c \
	gstglwindow_gpu_process.c

noinst_HEADERS = gstglwindow_gpu_process.h

libgstgl_gpuprocessincludedir = $(includedir)/gstreamer-@GST_API_VERSION@/gst/gl/gpuprocess
libgstgl_gpuprocessinclude_HEADERS = \
	gstglcontext_gpu_process.h \
	gstglwindow_gpu_process.h

libgstgl_gpuprocess_la_CFLAGS = \
	-I$(top_srcdir)/gst-libs \
	-I$(top_builddir)/gst-libs \
	$(GL_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS)

libgstgl_gpuprocess_la_LDFLAGS = \
	$(GST_LIB_LDFLAGS) \
	$(GST_ALL_LDFLAGS)

