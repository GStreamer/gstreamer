testprogs = loading dynamic linked static static2 # FIXME registry
 
filterdir = $(libdir)/gst

filter_LTLIBRARIES = libtestplugin.la libtestplugin2.la

libtestplugin_la_SOURCES = testplugin.c
libtestplugin2_la_SOURCES = testplugin2.c

linked_LDFLAGS = -L. -ltestplugin -ltestplugin2

static_SOURCES = static.c testplugin_s.c testplugin2_s.c

TESTS = $(testprogs)

check_PROGRAMS = $(testprogs)

# we have nothing but apps here, we can do this safely
LIBS = $(GST_LIBS)
CFLAGS = $(GLIB_CFLAGS) $(XML_CFLAGS) $(GST_CFLAGS)
