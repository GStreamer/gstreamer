bin_PROGRAMS = ges-launch
AM_CFLAGS =  -I$(top_srcdir) $(GST_PROFILE_CFLAGS) $(GST_DISCOVERER_CFLAGS) $(GST_CFLAGS)
LDADD = $(top_builddir)/ges/libges-@GST_MAJORMINOR@.la -lges-@GST_MAJORMINOR@ -lgstdiscoverer-@GST_MAJORMINOR@ $(GST_DISCOVERER_LIBS) $(GST_LIBS) 
