
plugin_LTLIBRARIES = \
	libgstv4l2element.la \
	libgstv4l2src.la

libgstv4l2element_la_SOURCES = gstv4l2element.c v4l2_calls.c v4l2-overlay_calls.c gstv4l2element-marshal.c
libgstv4l2element_la_CFLAGS = $(GST_CFLAGS)
libgstv4l2element_la_LIBADD =
libgstv4l2element_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstv4l2src_la_SOURCES = gstv4l2src.c v4l2src_calls.c
libgstv4l2src_la_CFLAGS = $(GST_CFLAGS)
libgstv4l2src_la_LIBADD = libgstv4l2element.la
libgstv4l2src_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstv4l2element.h v4l2_calls.h \
		gstv4l2src.h v4l2src_calls.h gstv4l2element-marshal.h

EXTRA_libgstv4l2element_la_SOURCES = \
	gstv4l2element-marshal.list

BUILT_SOURCES = \
	gstv4l2element-marshal.c \
	gstv4l2element-marshal.h

gstv4l2element-marshal.h: gstv4l2element-marshal.list
	glib-genmarshal --header --prefix=gstv4l2_cclosure_marshal $(srcdir)/gstv4l2element-marshal.list > gstv4l2element-marshal.h.tmp
	mv gstv4l2element-marshal.h.tmp gstv4l2element-marshal.h

gstv4l2element-marshal.c: gstv4l2element-marshal.list
	echo "#include \"glib.h\"" > gstv4l2element-marshal.c.tmp
	echo "#include \"glib-object.h\"" >> gstv4l2element-marshal.c.tmp
	echo "#include \"gstv4l2element-marshal.h\"" >> gstv4l2element-marshal.c.tmp
	glib-genmarshal --body --prefix=gstv4l2_cclosure_marshal $(srcdir)/gstv4l2element-marshal.list >> gstv4l2element-marshal.c.tmp
	mv gstv4l2element-marshal.c.tmp gstv4l2element-marshal.c

# Don't want the generated marshal files in the dist
dist-hook:
	rm -f $(distdir)/gstv4l2element-marshal.c
	rm -f $(distdir)/gstv4l2element-marshal.h

# Clean generated files
distclean-local:
	rm -f $(top_builddir)/src/element/gstv4l2element-marshal.c
	rm -f $(top_builddir)/src/element/gstv4l2element-marshal.h
