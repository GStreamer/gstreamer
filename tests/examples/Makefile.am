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
	assets \
	multifilesrc \
	$(graphical)

AM_CFLAGS =  -I$(top_srcdir) $(GST_PBUTILS_CFLAGS) $(GST_CFLAGS) $(GTK_CFLAGS)
AM_LDFLAGS = -export-dynamic
LDADD = $(top_builddir)/ges/libges-@GST_API_VERSION@.la $(GST_PBUTILS_LIBS) $(GST_LIBS) $(GTK_LIBS)
