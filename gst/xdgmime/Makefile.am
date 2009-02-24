plugin_LTLIBRARIES = libgstxdgmime.la

libgstxdgmime_la_SOURCES = gstxdgmime.c \
			   xdgmime/xdgmimealias.c \
			   xdgmime/xdgmime.c \
			   xdgmime/xdgmimecache.c \
			   xdgmime/xdgmimeglob.c \
			   xdgmime/xdgmimeicon.c \
			   xdgmime/xdgmimeint.c \
			   xdgmime/xdgmimemagic.c \
			   xdgmime/xdgmimeparent.c
libgstxdgmime_la_CFLAGS = $(GST_CFLAGS) -DXDG_PREFIX=gst_xdg_mime
libgstxdgmime_la_LIBADD = $(GST_LIBS)
libgstxdgmime_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstxdgmime_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = xdgmime/xdgmimealias.h \
		 xdgmime/xdgmimecache.h \
		 xdgmime/xdgmimeglob.h \
		 xdgmime/xdgmime.h \
		 xdgmime/xdgmimeicon.h \
		 xdgmime/xdgmimeint.h \
		 xdgmime/xdgmimemagic.h \
		 xdgmime/xdgmimeparent.h

EXTRA_DIST =
