plugindir = $(libdir)/gst
 
plugin_LTLIBRARIES = libgstgnomevfssrc.la libgstgnomevfssink.la
 
libgstgnomevfssrc_la_SOURCES = gstgnomevfssrc.c
libgstgnomevfssrc_la_CFLAGS = $(GST_CFLAGS) $(GNOME_VFS_CFLAGS)
libgstgnomevfssrc_la_LIBADD = $(GNOME_VFS_LIBS)
libgstgnomevfssrc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

libgstgnomevfssink_la_SOURCES = gstgnomevfssink.c
libgstgnomevfssink_la_CFLAGS = $(GST_CFLAGS) $(GNOME_VFS_CFLAGS)
libgstgnomevfssink_la_LIBADD = $(GNOME_VFS_LIBS)
libgstgnomevfssink_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
