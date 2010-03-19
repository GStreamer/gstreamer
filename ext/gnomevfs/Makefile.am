plugin_LTLIBRARIES = libgstgnomevfs.la
 
libgstgnomevfs_la_SOURCES = \
	gstgnomevfs.c \
	gstgnomevfssink.c \
	gstgnomevfssrc.c \
	gstgnomevfsuri.c

libgstgnomevfs_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) $(GST_CFLAGS) $(GNOME_VFS_CFLAGS)
libgstgnomevfs_la_LIBADD = \
	$(top_builddir)/gst-libs/gst/tag/libgsttag-$(GST_MAJORMINOR).la \
	$(GST_BASE_LIBS) $(GNOME_VFS_LIBS)
libgstgnomevfs_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstgnomevfs_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = \
	gstgnomevfs.h \
	gstgnomevfssink.h \
	gstgnomevfssrc.h \
	gstgnomevfsuri.h
