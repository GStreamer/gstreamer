plugin_LTLIBRARIES = libgstclosedcaption.la

zvbi_sources = \
	bit_slicer.c \
	decoder.c \
        raw_decoder.c \
	sampling_par.c

zvbi_headers = \
	bcd.h \
	bit_slicer.h \
	decoder.h \
	macros.h \
	misc.h \
	raw_decoder.h \
	sampling_par.h \
	sliced.h

libgstclosedcaption_la_SOURCES = \
	$(zvbi_sources) \
	$(zvbi_headers) \
	gstcccombiner.c \
	gstcccombiner.h \
	gstccextractor.c \
	gstccextractor.h \
	gstcea708decoder.c \
	gstcea708decoder.h \
	gstceaccoverlay.c \
	gstceaccoverlay.h \
	gstline21dec.c \
	gstline21dec.h \
	gstclosedcaption.c

libgstclosedcaption_la_CFLAGS = \
	$(GST_PLUGINS_BASE_CFLAGS) \
	$(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) \
	$(PANGO_CFLAGS)

libgstclosedcaption_la_LIBADD = \
	$(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) \
	$(GST_LIBS) \
	$(PANGO_LIBS)

libgstclosedcaption_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstccextractor.h \
	gstcccombiner.h \
	gstline21dec.h \
	$(zvbi_headers)
