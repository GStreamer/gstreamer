plugin_LTLIBRARIES = libgstlv2.la

libgstlv2_la_SOURCES = gstlv2.c
libgstlv2_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CONTROLLER_CFLAGS) $(GST_CFLAGS) $(SLV2_CFLAGS)
libgstlv2_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstaudio-$(GST_MAJORMINOR) $(LIBM) $(SLV2_LIBS) ../../gst-libs/gst/signalprocessor/libgstsignalprocessor.la
libgstlv2_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstlv2_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstlv2.h
