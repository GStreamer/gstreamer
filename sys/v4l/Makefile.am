filterdir = $(libdir)/gst

filter_LTLIBRARIES = libv4lelement.la libv4lsrc.la \
	libv4lmjpegsrc.la libv4lmjpegsink.la

libv4lelement_la_SOURCES = \
		gstv4lelement.c \
		v4l_calls.c
libv4lelement_la_CFLAGS = \
		$(GST_CFLAGS)

libv4lsrc_la_SOURCES = \
		gstv4lsrc.c \
		v4lsrc_calls.c
libv4lsrc_la_LIBADD = \
		libv4lelement.la
libv4lsrc_la_CFLAGS = \
		$(GST_CFLAGS)

libv4lmjpegsrc_la_SOURCES = \
		gstv4lmjpegsrc.c \
		v4lmjpegsrc_calls.c
libv4lmjpegsrc_la_LIBADD = \
		libv4lelement.la
libv4lmjpegsrc_la_CFLAGS = \
		$(GST_CFLAGS)

libv4lmjpegsink_la_SOURCES = \
		gstv4lmjpegsink.c \
		v4lmjpegsink_calls.c
libv4lmjpegsink_la_LIBADD = \
		libv4lelement.la
libv4lmjpegsink_la_CFLAGS = \
		$(GST_CFLAGS)

noinst_HEADERS = gstv4lelement.h v4l_calls.h \
		gstv4lsrc.h v4lsrc_calls.h \
		gstv4lmjpegsrc.h v4lmjpegsrc_calls.h \
		gstv4lmjpegsink.h v4lmjpegsink_calls.h
