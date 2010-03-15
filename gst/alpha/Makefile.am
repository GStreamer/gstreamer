plugin_LTLIBRARIES = libgstalpha.la libgstalphacolor.la

libgstalpha_la_SOURCES = gstalpha.c
libgstalpha_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GST_CONTROLLER_CFLAGS) 
libgstalpha_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) -lgstvideo-$(GST_MAJORMINOR) \
	$(GST_LIBS) $(GST_BASE_LIBS) $(LIBM) $(GST_CONTROLLER_LIBS)
libgstalpha_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstalpha_la_LIBTOOLFLAGS = --tag=disable-static

libgstalphacolor_la_SOURCES = gstalphacolor.c
libgstalphacolor_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) $(GST_CFLAGS)
libgstalphacolor_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	$(GST_BASE_LIBS) $(GST_LIBS) \
	-lgstvideo-@GST_MAJORMINOR@
libgstalphacolor_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstalphacolor_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gstalphacolor.h
