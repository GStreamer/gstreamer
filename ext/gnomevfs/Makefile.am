plugindir = $(libdir)/gst
 
plugin_LTLIBRARIES = libgstgnomevfssrc.la libgstgnomevfssink.la
 
libgstgnomevfssrc_la_SOURCES = gstgnomevfssrc.c
libgstgnomevfssink_la_SOURCES = gstgnomevfssink.c
 
libgstgnomevfssrc_la_CFLAGS = $(GNOME_VFS_CFLAGS) $(GST_CFLAGS)
libgstgnomevfssrc_la_LIBADD = $(GNOME_VFS_LIBS) $(GST_LIBS)

libgstgnomevfssink_la_CFLAGS = $(GNOME_VFS_CFLAGS) $(GST_CFLAGS)
libgstgnomevfssink_la_LIBADD = $(GNOME_VFS_LIBS) $(GST_LIBS)

