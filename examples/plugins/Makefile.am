
plugin_LTLIBRARIES = libgstexample.la 

libgstexample_la_SOURCES = example.c
libgstexample_la_CFLAGS = $(GST_CFLAGS)
libgstexample_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = example.h 

## ... but we don't want them installed so we override the install target
install:
	@true
