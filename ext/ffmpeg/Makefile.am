plugindir = $(libdir)/gstreamer-@GST_MAJORMINOR@

plugin_LTLIBRARIES = libgstffmpeg.la

libgstffmpeg_la_SOURCES = gstffmpeg.c 		\
			  gstffmpegcodecmap.c	\
			  gstffmpegdec.c 	\
			  gstffmpegdemux.c 	\
			  gstffmpegenc.c 	\
			  gstffmpegmux.c 	\
			  gstffmpegprotocol.c 	\
			  gstffmpegtypes.c 	

libgstffmpeg_la_CFLAGS = $(GST_CFLAGS) $(FFMPEG_CFLAGS)
libgstffmpeg_la_LIBADD = 
libgstffmpeg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) $(FFMPEG_LIBS)

noinst_HEADERS = 
