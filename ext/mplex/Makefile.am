plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

CC = g++

EXTRA_DIST = \
	README  	INSTRUCT	TODO \
	.cvsignore

MAINTAINERCLEANFILES = Makefile.in

gstmplex_SOURCES = \
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

plugin_LTLIBRARIES = libgstmplex.la

libgstmplex_la_SOURCES = gstmplex.cc \
                        $(gstmplex_SOURCES)
                           
libgstmplex_la_CXXFLAGS = $(GST_CFLAGS) 
libgstmplex_la_LIBADD = -lstdc++ $(LIBM_LIBS)
libgstmplex_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

noinst_HEADERS = gstmplex.h


