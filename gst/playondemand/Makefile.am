
plugin_LTLIBRARIES = libgstplayondemand.la

libgstplayondemand_la_SOURCES = gstplayondemand.c
libgstplayondemand_la_CFLAGS = $(GST_CFLAGS)
libgstplayondemand_la_LIBADD =
libgstplayondemand_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstplayondemand.h filter.func

if HAVE_GTK
noinst_PROGRAMS = demo_mp3
endif

demo_mp3_SOURCES = demo-mp3.c gstplayondemand.h
## putting GTK_CFLAGS first fixes a weird compilation error with GTK and XML
demo_mp3_CFLAGS  = $(GTK_CFLAGS) $(GST_CFLAGS)
demo_mp3_LDFLAGS = $(GST_LIBS) $(GTK_LIBS)
demo_mp3_DEPENDENCIES = libgstplayondemand.la
