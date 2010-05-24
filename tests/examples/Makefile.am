noinst_PROGRAMS = 	\
	concatenate	\
	playlist	\
	simple1		\
	test1		\
	test2		\
	test3		\
	test4		\
	transition

AM_CFLAGS =  -I$(top_srcdir) $(GST_PROFILE_CFLAGS) $(GST_DISCOVERER_CFLAGS) $(GST_CFLAGS)
LDADD = $(top_builddir)/ges/libges-@GST_MAJORMINOR@.la -lges-@GST_MAJORMINOR@ -lgstdiscoverer-@GST_MAJORMINOR@ $(GST_DISCOVERER_LIBS) $(GST_LIBS) 
