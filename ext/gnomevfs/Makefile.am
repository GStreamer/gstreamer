plugin_LTLIBRARIES = libgstgnomevfs.la
 
libgstgnomevfs_la_SOURCES = \
	gstgnomevfs.c \
	gstgnomevfssrc.c \
	gstgnomevfssink.c \
	gstgnomevfsuri.c
libgstgnomevfs_la_CFLAGS = $(GST_CFLAGS) $(GNOME_VFS_CFLAGS)
libgstgnomevfs_la_LIBADD = $(GNOME_VFS_LIBS)
libgstgnomevfs_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstgnomevfs.h \
	gstgnomevfsuri.h
