plugin_LTLIBRARIES = libgstmpegtsparse.la

libgstmpegtsparse_la_SOURCES = \
  mpegtsparse.c \
  mpegtspacketizer.c\
  mpegtsparsemarshal.c \
  flutspatinfo.c \
  flutspmtinfo.c \
  flutspmtstreaminfo.c

BUILT_SOURCES = \
  mpegtsparsemarshal.h \
  mpegtsparsemarshal.c
CLEANFILES = $(BUILT_SOURCES)

EXTRA_libgstmpegtsparse_la_SOURCES = \
  mpegtsparsemarshal.list

mpegtsparsemarshal.h: mpegtsparsemarshal.list
	glib-genmarshal --header --prefix=mpegts_parse_marshal $(srcdir)/mpegtsparsemarshal.list > mpegtsparsemarshal.h.tmp
	mv mpegtsparsemarshal.h.tmp mpegtsparsemarshal.h

mpegtsparsemarshal.c: mpegtsparsemarshal.list mpegtsparsemarshal.h
	echo "#include \"mpegtsparsemarshal.h\"" >> mpegtsparsemarshal.c.tmp
	glib-genmarshal --body --prefix=mpegts_parse_marshal $(srcdir)/mpegtsparsemarshal.list >> mpegtsparsemarshal.c.tmp
	mv mpegtsparsemarshal.c.tmp mpegtsparsemarshal.c

libgstmpegtsparse_la_CFLAGS = $(GST_CFLAGS)
libgstmpegtsparse_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstmpegtsparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
  mpegtsparse.h \
  mpegtsparsemarshal.h \
  mpegtspacketizer.h \
  flutspatinfo.h \
  flutspmtinfo.h \
  flutspmtstreaminfo.h
