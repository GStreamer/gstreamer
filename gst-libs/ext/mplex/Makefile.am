
EXTRA_DIST = README INSTRUCT TODO

noinst_LTLIBRARIES = libmplex.la

libmplex_la_SOURCES = \
	bits.cc			bits.hh	\
	buffer.cc		buffer.hh \
	videostrm.hh	audiostrm.hh	padstrm.hh \
	videostrm_in.cc	 padstrm.cc \
	lpcmstrm_in.cc mpastrm_in.cc	ac3strm_in.cc \
	videostrm_out.cc	audiostrm_out.cc	\
	multplex.cc	outputstream.hh	\
	systems.cc		systems.hh \
	vector.cc	vector.hh	mplexconsts.hh \
	inputstrm.cc inputstrm.hh aunit.hh \
	stillsstream.cc stillsstream.hh \
	mjpeg_logging.cc mjpeg_logging.h \
	yuv4mpeg.cc yuv4mpeg_ratio.cc yuv4mpeg.h \
	mpegconsts.cc mpegconsts.h

libmplex_la_CXXFLAGS = $(GST_CFLAGS) 
libmplex_la_LIBADD = -lstdc++ $(LIBM_LIBS)
libmplex_la_LDFLAGS = 


