
plugin_LTLIBRARIES = libgstgdkpixbuf.la

libgstgdkpixbuf_la_SOURCES = gstgdkpixbuf.c
libgstgdkpixbuf_la_CFLAGS = $(GST_CFLAGS) $(GTK_CFLAGS)
libgstgdkpixbuf_la_LIBADD = $(GTK_LIBS)
libgstgdkpixbuf_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)


if HAVE_GDK_LOADERS
loaderdir = $(GDK_PIXBUF_LOADER_DIR)
loader_LTLIBRARIES = gst_loader.la

gst_loader_la_CFLAGS = $(GST_CFLAGS) $(GTK_CFLAGS) -DGDK_PIXBUF_ENABLE_BACKEND
gst_loader_la_SOURCES = 	\
	gstgdkanimation.c	\
	gst_loader.c
gst_loader_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) --avoid-version --module
gst_loader_la_LIBADD = $(GTK_LIBS) $(GST_LIBS)

gst_loader_headers = gstgdkanimation.h

#install-data-hook:
#	if test -z "$(DESTDIR)"  ; then
#	  $(mkinstalldirs) $(DESTDIR)$(GDK_PIXBUF_SYSCONFDIR)/gtk-2.0 ; \
#	  $(QUERYLOADERS) > $(DESTDIR)$(GDK_PIXBUF_SYSCONFDIR)/gtk-2.0/gdk-pixbuf.loaders ; \
#	fi

else
gst_loader_headers =
endif


noinst_HEADERS = gstgdkpixbuf.h \
	$(gst_loader_headers)
