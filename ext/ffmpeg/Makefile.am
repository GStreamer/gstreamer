plugindir = $(libdir)/gst

plugin_LTLIBRARIES = libgstffmpeg.la

libgstffmpeg_la_SOURCES = gstffmpeg.c 		\
			  gstffmpegenc.c 	\
			  gstffmpegdec.c 	\
			  gstffmpegdemux.c 	\
			  gstffmpegmux.c 	\
			  gstffmpegprotocol.c 	\
			  gstffmpegcodecmap.c

libgstffmpeg_la_CFLAGS = $(GST_CFLAGS) -I/opt/src/sourceforge/ffmpeg/ -I/opt/src/sourceforge/ffmpeg/libavcodec/
libgstffmpeg_la_LIBADD = 
libgstffmpeg_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) -L/opt/src/sourceforge/ffmpeg/libavcodec -lavcodec -L/opt/src/sourceforge/ffmpeg/libav -lavformat

noinst_HEADERS = 
