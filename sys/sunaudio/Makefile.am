plugin_LTLIBRARIES = libgstsunaudio.la

libgstsunaudio_la_SOURCES = gstsunaudio.c   \
			    gstsunaudiosrc.c \
                            gstsunelement.c \
                            gstsunmixer.c


libgstsunaudio_la_CFLAGS = $(GST_CFLAGS)
libgstsunaudio_la_LIBADD = $(top_builddir)/gst-libs/gst/libgstinterfaces-@GST_MAJORMINOR@.la
libgstsunaudio_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstsunelement.h \
		 gstsunaudiosrc.h \
                 gstsunmixer.h

