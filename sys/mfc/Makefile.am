plugin_LTLIBRARIES = libgstmfc.la

libgstmfc_la_SOURCES = \
	mfc_decoder/mfc_decoder.c \
	fimc/fimc.c \
	gstmfc.c \
	gstmfcdec.c

noinst_HEADERS = \
	mfc_decoder/mfc_decoder.h \
	fimc/fimc.h \
	gstmfcdec.h

libgstmfc_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	-I$(srcdir)
libgstmfc_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) \
	-lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS)
libgstmfc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmfc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

