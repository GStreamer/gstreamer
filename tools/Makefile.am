bin_PROGRAMS = ges-launch-@GST_API_VERSION@

AM_CFLAGS =  -I$(top_srcdir) $(GST_PBUTILS_CFLAGS) $(GST_CFLAGS) $(GIO_CFLAGS) $(GST_VALIDATE_CFLAGS)
LDADD = $(top_builddir)/ges/libges-@GST_API_VERSION@.la $(GST_PBUTILS_LIBS) $(GST_LIBS) $(GIO_LIBS) $(GST_VALIDATE_LIBS)

noinst_HEADERS = ges-validate.h ges-launcher.h utils.h

ges_launch_@GST_API_VERSION@_SOURCES = ges-validate.c ges-launch.c ges-launcher.c utils.c
