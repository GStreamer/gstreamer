noinst_PROGRAMS = 	\
	simple1		\
	test1		\
	test2		\
	test3		\
	test4

AM_CFLAGS =  -I$(top_srcdir) $(GST_PROFILE_CFLAGS) $(GST_CFLAGS)
LDADD = $(top_builddir)/ges/libges-@GST_MAJORMINOR@.la -lges-@GST_MAJORMINOR@ $(GST_PROFILE_LIBS) $(GST_LIBS) 
