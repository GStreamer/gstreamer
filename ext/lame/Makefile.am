plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstlame.la

libgstlame_la_SOURCES = gstlame.c
libgstlame_la_CFLAGS = $(GST_CFLAGS)
libgstlame_la_LIBADD = $(LAME_LIBS)
libgstlame_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstlame.h

CFLAGS = $(GST_CFLAGS)
LIBS = $(GST_LIBS) $(LAME_LIBS)

## testprogs = test-lame
## TESTS = $(testprogs)
## check_PROGRAMS = $(testprogs)

## $(TESTS):
##	@echo -e '\nrunning gst-register for tests...\n'
##	$(GST_TOOLS_DIR)/gst-register --gst-mask=0 --gst-plugin-path=$(GST_PLUGIN_PATH)
		
