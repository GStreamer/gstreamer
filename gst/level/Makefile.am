
plugin_LTLIBRARIES = libgstlevel.la

libgstlevel_la_SOURCES = gstlevel.c gstlevel-marshal.c
libgstlevel_la_CFLAGS = $(GST_CFLAGS)
libgstlevel_la_LIBADD =
libgstlevel_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstlevel.h filter.func

if HAVE_GTK
noinst_PROGRAMS = demo plot
demo_SOURCES = demo.c
demo_CFLAGS = $(GTK_CFLAGS) $(GST_CFLAGS)
demo_LDFLAGS = $(GTK_LIBS) $(GST_LIBS)
plot_SOURCES = plot.c
plot_CFLAGS = $(GTK_CFLAGS) $(GST_CFLAGS)
plot_LDFLAGS = $(GTK_LIBS) $(GST_LIBS)
endif

EXTRA_libgstlevel_la_SOURCES = gstlevel-marshal.list

BUILT_SOURCES = \
	gstlevel-marshal.c \
	gstlevel-marshal.h
CLEANFILES = \
	gstlevel-marshal.c \
	gstlevel-marshal.h
			     

gstlevel-marshal.h: gstlevel-marshal.list
	glib-genmarshal --header --prefix=gstlevel_cclosure_marshal $(srcdir)/gstlevel-marshal.list > gstlevel-marshal.h.tmp
	mv gstlevel-marshal.h.tmp gstlevel-marshal.h
                                                                                
gstlevel-marshal.c: gstlevel-marshal.list
	echo "#include \"glib.h\"" > gstlevel-marshal.c.tmp
	echo "#include \"glib-object.h\"" >> gstlevel-marshal.c.tmp
	echo "#include \"gstlevel-marshal.h\"" >> gstlevel-marshal.c.tmp
	glib-genmarshal --body --prefix=gstlevel_cclosure_marshal $(srcdir)/gstlevel-marshal.list >> gstlevel-marshal.c.tmp
	mv gstlevel-marshal.c.tmp gstlevel-marshal.c
                                                                                
# Don't want the generated marshal files in the dist
dist-hook:
	rm -f $(distdir)/gstlevel-marshal.c
	rm -f $(distdir)/gstlevel-marshal.h

EXTRA_DIST = README
