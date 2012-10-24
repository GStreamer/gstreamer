plugin_LTLIBRARIES = libgstmpeg2enc.la

libgstmpeg2enc_la_SOURCES = \
	gstmpeg2enc.cc \
	gstmpeg2encoptions.cc \
	gstmpeg2encoder.cc \
	gstmpeg2encstreamwriter.cc \
	gstmpeg2encpicturereader.cc

libgstmpeg2enc_la_CXXFLAGS = \
	$(GST_PLUGINS_BAD_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS) $(GST_CXXFLAGS) $(MPEG2ENC_CFLAGS)
libgstmpeg2enc_la_LIBADD = \
        $(GST_PLUGINS_BASE_LIBS) -lgstvideo-@GST_API_VERSION@ \
        $(GST_LIBS) $(MPEG2ENC_LIBS)
libgstmpeg2enc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgstmpeg2enc_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS = \
	gstmpeg2enc.hh \
	gstmpeg2encoder.hh \
	gstmpeg2encoptions.hh \
	gstmpeg2encstreamwriter.hh \
	gstmpeg2encpicturereader.hh
