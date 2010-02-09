noinst_PROGRAMS = test1 test2 test3 test4

AM_CFLAGS = $(GST_PROFILE_CFLAGS) $(GST_CFLAGS) -I$(top_srcdir)
LDADD = $(GST_PROFILE_LIBS) $(GST_LIBS) $(top_builddir)/ges/libges-@GST_MAJORMINOR@.la -lges-@GST_MAJORMINOR@
