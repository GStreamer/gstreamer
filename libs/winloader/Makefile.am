filterdir = $(libdir)/gst

filter_LTLIBRARIES = libwinloader.la

libwinloader_la_SOURCES = driver.c elfdll.c ext.c externals.c module.c pe_image.c pe_resource.c registry.c resource.c stubs.s vfl.c afl.c

noinst_HEADERS = externals.h

DEFINES=-rdynamic -fPIC -D__WINE__ -Ddbg_printf=__vprintf -DTRACE=__vprintf 
CFLAGS += $(DEFINES) 

INCLUDES = $(GLIB_CFLAGS) $(GTK_CFLAGS) -I$(top_srcdir) -I$(top_srcdir)/include
LDADD = $(GLIB_LIBS) $(GTK_LIBS) $(top_srcdir)/gst/libgst.la
