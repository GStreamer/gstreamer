if HAVE_GTK_X11
graphical=ges-ui
else
graphical=
endif

noinst_PROGRAMS = 	\
	concatenate	\
	simple1		\
	test1		\
	test2		\
	test3		\
	test4		\
	transition	\
	thumbnails	\
	overlays	\
	text_properties	\
	$(graphical)

AM_CFLAGS =  -I$(top_srcdir) $(GST_PROFILE_CFLAGS) $(GST_DISCOVERER_CFLAGS) $(GST_CFLAGS) $(GTK_CFLAGS)
LDADD = $(top_builddir)/ges/libges-@GST_MAJORMINOR@.la -lges-@GST_MAJORMINOR@ -lgstdiscoverer-@GST_MAJORMINOR@ $(GST_DISCOVERER_LIBS) $(GST_LIBS) $(GTK_LIBS)
