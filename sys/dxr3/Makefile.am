
plugin_LTLIBRARIES = libgstdxr3.la

libgstdxr3_la_SOURCES = dxr3videosink.c dxr3audiosink.c dxr3spusink.c \
			dxr3init.c ac3_padder.c 
noinst_libgstdxr3_la_SOURCES = dxr3marshal.c
libgstdxr3_la_CFLAGS = $(GST_CFLAGS)
libgstdxr3_la_LIBADD =
libgstdxr3_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

BUILT_SOURCES = dxr3marshal.c dxr3marshal.h
built_headers =	dxr3marshal.h

noinst_HEADERS = dxr3videosink.h dxr3audiosink.h dxr3spusink.h \
		 dxr3common.h ac3_padder.h

EXTRA_DIST = dxr3marshal.list
	

dxr3marshal.h: dxr3marshal.list
	glib-genmarshal --header --prefix=dxr3_marshal $^ > dxr3marshal.h.tmp
	mv dxr3marshal.h.tmp dxr3marshal.h

dxr3marshal.c: dxr3marshal.list
	echo "#include \"dxr3marshal.h\"" >> dxr3marshal.c.tmp
	glib-genmarshal --body --prefix=dxr3_marshal $^ >> dxr3marshal.c.tmp
	mv dxr3marshal.c.tmp dxr3marshal.c

CLEANFILES = $(BUILT_SOURCES)
