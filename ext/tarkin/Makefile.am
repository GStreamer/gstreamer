
plugin_LTLIBRARIES = libgsttarkin.la

libgsttarkin_la_SOURCES = tarkin.c \
			  mem.c wavelet.c wavelet_xform.c \
			  wavelet_coeff.c yuv.c info.c \
			  gsttarkin.c gsttarkinenc.c gsttarkindec.c

libgsttarkin_la_CFLAGS = $(GST_CFLAGS) $(VORBIS_CFLAGS) -DTYPE_BITS=10 -DTYPE=int16_t -DRLECODER 
## AM_PATH_VORBIS also sets VORBISENC_LIBS
libgsttarkin_la_LIBADD = $(VORBIS_LIBS) $(VORBISENC_LIBS)
libgsttarkin_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgsttarkin_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS = gsttarkinenc.h mem.h tarkin.h yuv.h wavelet.h
