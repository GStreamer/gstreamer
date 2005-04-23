
plugin_LTLIBRARIES = libgstspider.la

libgstspider_la_SOURCES = \
	gstspider.c gstspideridentity.c \
	gstsearchfuncs.c
libgstspider_la_CFLAGS = $(GST_OBJ_CFLAGS)
libgstspider_la_LIBADD = $(GST_OBJ_LIBS)
libgstspider_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstspider.h gstspideridentity.h gstsearchfuncs.h 

noinst_PROGRAMS = spidertest

spidertest_SOURCES = spidertest.c
spidertest_CFLAGS = $(GST_OBJ_CFLAGS)
spidertest_LDADD = $(GST_OBJ_LIBS)

