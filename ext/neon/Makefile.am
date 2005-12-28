plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES =	libgstneonhttpsrc.la

libgstneonhttpsrc_la_SOURCES =	gstneonhttpsrc.c


libgstneonhttpsrc_la_CFLAGS = 	$(GST_CFLAGS) $(NEON_CFLAGS) 
libgstneonhttpsrc_la_LDFLAGS = 	$(GST_PLUGIN_LDFLAGS)
libgstneonhttpsrc_la_LIBADD = 	$(GST_BASE_LIBS) \
				$(NEON_LIBS)

noinst_HEADERS = gstneonhttpsrc.h