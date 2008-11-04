plugin_LTLIBRARIES = libgstalaw.la libgstmulaw.la

libgstalaw_la_SOURCES = alaw-encode.c alaw-decode.c alaw.c
libgstalaw_la_CFLAGS = $(GST_CFLAGS)
libgstalaw_la_LIBADD = $(GST_LIBS)
libgstalaw_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstalaw_la_LIBTOOLFLAGS = --tag=disable-static

libgstmulaw_la_SOURCES = mulaw-encode.c mulaw-conversion.c mulaw-decode.c mulaw.c
libgstmulaw_la_CFLAGS = $(GST_CFLAGS)
libgstmulaw_la_LIBADD = $(GST_LIBS)
libgstmulaw_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmulaw_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = mulaw-conversion.h alaw-encode.h alaw-decode.h mulaw-encode.h mulaw-decode.h
