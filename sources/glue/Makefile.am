lib_LTLIBRARIES = libgstreamersharpglue-1.0.0.la

libgstreamersharpglue_1_0_0_la_SOURCES = gobject.c

nodist_libgstreamersharpglue_1_0_0_la_SOURCES = generated.c

libgstreamersharpglue_1_0_0_la_LDFLAGS = -module -avoid-version -no-undefined

libgstreamersharpglue_1_0_0_la_LIBADD = $(GST_LIBS)

libgstreamersharpglue_1_0_0_la_CPPFLAGS = -DGST_USE_UNSTABLE_API

INCLUDES = $(GST_CFLAGS) -I$(top_srcdir)

libgstreamersharpglue.dll: $(libgstreamersharpglue_1_0_0_la_OBJECTS) libgstreamersharpglue.rc libgstreamersharpglue.def
	./build-dll libgstreamersharpglue $(VERSION)

CLEANFILES = lib*.a lib*.dll generated.c

EXTRA_DIST =
