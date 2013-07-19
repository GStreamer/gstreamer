lib_LTLIBRARIES = libgstsharpglue-1.0.6.la
# TODO: Select all files except generated.c dynamically
libgstsharpglue_1_0_6_la_SOURCES = gobject.c

nodist_libgstsharpglue_1_0_6_la_SOURCES = generated.c

libgstsharpglue_1_0_6_la_LDFLAGS = -module -avoid-version -no-undefined

libgstsharpglue_1_0_6_la_LIBADD = $(GST_LIBS)

INCLUDES = $(GST_CFLAGS) -I$(top_srcdir)

libgstsharpglue.dll: $(libgstsharpglue_1_0_6_la_OBJECTS) libgstsharpglue.rc libgstsharpglue.def
	./build-dll libgstsharpglue $(VERSION)

CLEANFILES = lib*.a lib*.dll

EXTRA_DIST =
