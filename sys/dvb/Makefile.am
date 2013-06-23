
plugin_LTLIBRARIES = libgstdvb.la

libgstdvb_la_SOURCES = \
	gstdvb.c \
	gstdvbsrc.c \
	dvbbasebin.c \
	cam.c \
	camdevice.c \
	camswclient.c \
	camutils.c \
	camtransport.c \
	camsession.c \
	camapplication.c \
	camresourcemanager.c \
	camapplicationinfo.c \
	camconditionalaccess.c \
	parsechannels.c

libgstdvb_la_CFLAGS = $(GST_PLUGINS_BAD_CFLAGS) $(GST_CFLAGS)
libgstdvb_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/mpegts/libgstmpegts-$(GST_API_VERSION).la \
	$(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS)
libgstdvb_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstdvb_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstdvbsrc.h  \
	dvbbasebin.h \
	cam.h \
	camdevice.h \
	camswclient.h \
	camutils.h \
	camtransport.h \
	camsession.h \
	camapplication.h \
	camresourcemanager.h \
	camapplicationinfo.h \
	camconditionalaccess.h \
	parsechannels.h
