noinst_PROGRAMS = timeline

AM_CFLAGS =  -I$(top_srcdir) $(GST_PBUTILS_CFLAGS) $(GST_CFLAGS)
AM_LDFLAGS = -export-dynamic
LDADD = $(top_builddir)/ges/libges-@GST_API_VERSION@.la $(GST_PBUTILS_LIBS) $(GST_LIBS)
