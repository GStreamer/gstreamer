plugin_LTLIBRARIES = libgstmpeg2enc.la

libgstmpeg2enc_la_SOURCES = \
	gstmpeg2enc.cc \
	gstmpeg2encoptions.cc \
	gstmpeg2encoder.cc \
	gstmpeg2encstreamwriter.cc \
	gstmpeg2encpicturereader.cc
libgstmpeg2enc_la_CXXFLAGS = $(MPEG2ENC_CFLAGS) $(GST_CFLAGS)
libgstmpeg2enc_la_LIBADD = $(MPEG2ENC_LIBS)
libgstmpeg2enc_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = \
	gstmpeg2enc.hh \
	gstmpeg2encoder.hh \
	gstmpeg2encoptions.hh \
	gstmpeg2encstreamwriter.hh \
	gstmpeg2encpicturereader.hh
