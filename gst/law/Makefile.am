filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgstalaw.la libgstmulaw.la

libgstalaw_la_SOURCES = alaw-encode.c mulaw-conversion.c alaw-conversion.c alaw-decode.c alaw.c
libgstalaw_la_CFLAGS = $(GST_CFLAGS)

libgstmulaw_la_SOURCES = mulaw-encode.c mulaw-conversion.c mulaw-decode.c mulaw.c
libgstmulaw_la_CFLAGS = $(GST_CFLAGS)

noinst_HEADERS = mulaw-conversion.h alaw-encode.h alaw-decode.h alaw-conversion.h mulaw-encode.h mulaw-decode.h
filterdir = $(libdir)/gst
