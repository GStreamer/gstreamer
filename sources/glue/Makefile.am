lib_LTLIBRARIES = libgstreamersharpglue-1.0.6.la

libgstreamersharpglue_1_0_6_la_SOURCES = gobject.c

nodist_libgstreamersharpglue_1_0_6_la_SOURCES = generated.c

libgstreamersharpglue_1_0_6_la_LDFLAGS = -module -avoid-version -no-undefined

libgstreamersharpglue_1_0_6_la_LIBADD = $(GST_LIBS)

INCLUDES = $(GST_CFLAGS) -I$(top_srcdir)

libgstreamersharpglue.dll: $(libgstreamersharpglue_1_0_6_la_OBJECTS) libgstreamersharpglue.rc libgstreamersharpglue.def
	./build-dll libgstreamersharpglue $(VERSION)

CLEANFILES = lib*.a lib*.dll

EXTRA_DIST =
