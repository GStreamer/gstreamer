
plugin_LTLIBRARIES = libgstvideo4linux.la

libgstvideo4linux_la_SOURCES = \
	gstv4lelement.c v4l_calls.c \
	v4l-overlay_calls.c \
	gstv4lelement-marshal.c \
	gstv4lsrc.c v4lsrc_calls.c \
	gstv4lmjpegsrc.c v4lmjpegsrc_calls.c \
	gstv4lmjpegsink.c v4lmjpegsink_calls.c \
	gstv4l.c
libgstvideo4linux_la_CFLAGS = $(GST_CFLAGS)
libgstvideo4linux_la_LIBADD =
libgstvideo4linux_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstv4lelement.h v4l_calls.h \
		gstv4lsrc.h v4lsrc_calls.h \
		gstv4lmjpegsrc.h v4lmjpegsrc_calls.h \
		gstv4lmjpegsink.h v4lmjpegsink_calls.h \
		videodev_mjpeg.h gstv4lelement-marshal.h

EXTRA_libgstvideo4linux_la_SOURCES = \
	gstv4lelement-marshal.list

BUILT_SOURCES = \
	gstv4lelement-marshal.c \
	gstv4lelement-marshal.h

gstv4lelement-marshal.h: gstv4lelement-marshal.list
	glib-genmarshal --header --prefix=gstv4l_cclosure_marshal $(srcdir)/gstv4lelement-marshal.list > gstv4lelement-marshal.h.tmp
	mv gstv4lelement-marshal.h.tmp gstv4lelement-marshal.h

gstv4lelement-marshal.c: gstv4lelement-marshal.list
	echo "#include \"glib.h\"" > gstv4lelement-marshal.c.tmp
	echo "#include \"glib-object.h\"" >> gstv4lelement-marshal.c.tmp
	echo "#include \"gstv4lelement-marshal.h\"" >> gstv4lelement-marshal.c.tmp
	glib-genmarshal --body --prefix=gstv4l_cclosure_marshal $(srcdir)/gstv4lelement-marshal.list >> gstv4lelement-marshal.c.tmp
	mv gstv4lelement-marshal.c.tmp gstv4lelement-marshal.c

# Don't want the generated marshal files in the dist
dist-hook:
	rm -f $(distdir)/gstv4lelement-marshal.c
	rm -f $(distdir)/gstv4lelement-marshal.h

# Clean generated files
distclean-local:
	rm -f $(top_builddir)/src/element/gstv4lelement-marshal.c
	rm -f $(top_builddir)/src/element/gstv4lelement-marshal.h
