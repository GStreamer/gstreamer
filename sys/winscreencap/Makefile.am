plugin_LTLIBRARIES = libgstwinscreencap.la

libgstwinscreencap_la_SOURCES = gstdx9screencapsrc.c  gstgdiscreencapsrc.c  gstwinscreencap.c
libgstwinscreencap_la_CFLAGS = $(GST_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS) $(WINSCREENCAP_CFLAGS) $(DIRECTX_CFLAGS)
libgstwinscreencap_la_LIBADD = $(GST_BASE_LIBS) $(GST_PLUGINS_BASE_LIBS) \
	$(WINSCREENCAP_LIBS) $(DIRECTX_LDFLAGS)
libgstwinscreencap_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS= gstwinscreencap.h gstdx9screencapsrc.h gstgdiscreencapsrc.h
