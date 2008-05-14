
plugin_LTLIBRARIES = libgstinterleave.la

libgstinterleave_la_SOURCES = plugin.c interleave.c deinterleave.c
libgstinterleave_la_CFLAGS = $(GST_CFLAGS)
libgstinterleave_la_LIBADD = $(GST_LIBS)
libgstinterleave_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = plugin.h interleave.h deinterleave.h
