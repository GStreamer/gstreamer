plugin_LTLIBRARIES = libgsthalelements.la

libgsthalelements_la_SOURCES = \
	gsthalaudiosink.c \
	gsthalaudiosrc.c \
	gsthalelements.c \
	hal.c

libgsthalelements_la_CFLAGS = $(GST_CFLAGS) $(HAL_CFLAGS)
libgsthalelements_la_LIBADD = $(GST_LIBS) $(HAL_LIBS)
libgsthalelements_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gsthalaudiosink.h \
	gsthalaudiosrc.h \
	gsthalelements.h \
	hal.h
