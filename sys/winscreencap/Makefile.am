plugin_LTLIBRARIES = libgstwinscreencap.la

libgstwinscreencap_la_SOURCES = gstdx9screencapsrc.c  gstgdiscreencapsrc.c  gstwinscreencap.c
libgstwinscreencap_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(DIRECT3D9_CFLAGS) $(DIRECTX_CFLAGS)
libgstwinscreencap_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	$(DIRECT3D9_LIBS) $(DIRECTX_LDFLAGS)
libgstwinscreencap_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstwinscreencap_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS= gstwinscreencap.h
