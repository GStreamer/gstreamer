plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstalaw.la libgstmulaw.la

libgstalaw_la_SOURCES = alaw-encode.c mulaw-conversion.c alaw-conversion.c alaw-decode.c alaw.c
libgstalaw_la_CFLAGS = $(GST_CFLAGS)
libgstalaw_la_LIBADD =
libgstalaw_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstmulaw_la_SOURCES = mulaw-encode.c mulaw-conversion.c mulaw-decode.c mulaw.c
libgstmulaw_la_CFLAGS = $(GST_CFLAGS)
libgstmulaw_la_LIBADD =
libgstmulaw_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = mulaw-conversion.h alaw-encode.h alaw-decode.h alaw-conversion.h mulaw-encode.h mulaw-decode.h
