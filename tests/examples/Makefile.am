noinst_PROGRAMS = test1 test2 test3 test4

AM_CFLAGS = $(GST_CONVENIENCE_CFLAGS) $(GST_CFLAGS) -I$(top_srcdir)
LDADD = $(GST_CONVENIENCE_LIBS) -lgstprofile-@GST_MAJORMINOR@ $(GST_LBS) $(top_builddir)/ges/libges-@GST_MAJORMINOR@.la -lges-@GST_MAJORMINOR@
