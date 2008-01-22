plugin_LTLIBRARIES = libgstmpegtsparse.la

libgstmpegtsparse_la_SOURCES = \
  mpegtsparse.c \
  mpegtspacketizer.c \
  gstmpegdesc.c

libgstmpegtsparse_la_CFLAGS = $(GST_CFLAGS)
libgstmpegtsparse_la_LIBADD = $(GST_BASE_LIBS) $(GST_LIBS)
libgstmpegtsparse_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
  mpegtsparse.h \
  mpegtspacketizer.h \
  gstmpegdesc.h
