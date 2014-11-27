plugin_LTLIBRARIES = libgstapplemedia.la

libgstapplemedia_la_SOURCES =			\
	plugin.m				\
	vtutil.c				\
	corevideobuffer.c			\
	coremediabuffer.c			\
	atdec.c

libgstapplemedia_la_CPPFLAGS =			\
	-Dgst_core_media_buffer_new=gst_core_media_buffer_priv_new		\
	-Dgst_core_media_buffer_get_type=gst_core_media_buffer_priv_get_type	\
	-Dgst_core_media_buffer_get_pixel_buffer=gst_core_media_buffer_priv_get_pixel_buffer\
	-Dgst_core_video_buffer_new=gst_core_video_buffer_priv_new		\
	-Dgst_core_video_buffer_get_type=gst_core_video_buffer_priv_get_type

libgstapplemedia_la_CFLAGS =			\
	$(GST_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(GST_PLUGINS_BASE_CFLAGS)

libgstapplemedia_la_OBJCFLAGS =			\
	$(GST_OBJCFLAGS)			\
	$(GST_BASE_CFLAGS)			\
	$(GST_PLUGINS_BASE_CFLAGS)

if HAVE_IOS
libgstapplemedia_la_OBJCFLAGS += -fobjc-abi-version=2 -fobjc-legacy-dispatch
endif

libgstapplemedia_la_LIBADD =			\
	$(GST_BASE_LIBS)			\
	$(GST_PLUGINS_BASE_LIBS)		\
	-lgstvideo-$(GST_API_VERSION) 		\
	-lgstaudio-$(GST_API_VERSION)		\
	-lgstpbutils-$(GST_API_VERSION) 	\
	$(GMODULE_NO_EXPORT_LIBS)

libgstapplemedia_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS) --tag=CC

libgstapplemedia_la_LDFLAGS =			\
	$(GST_PLUGIN_LDFLAGS)			\
	$(GST_BASE_LDFLAGS)			\
	$(GST_PLUGINS_BASE_LDFLAGS)		\
	-Wl,-framework -Wl,CoreFoundation	\
	-Wl,-framework -Wl,CoreMedia		\
	-Wl,-framework -Wl,CoreVideo 		\
	-Wl,-framework -Wl,AudioToolbox

noinst_HEADERS =				\
	qtkitvideosrc.h				\
	avfvideosrc.h				\
	vtenc.h					\
	vtdec.h					\
	vtutil.h				\
	corevideobuffer.h			\
	coremediabuffer.h			\
	atdec.h 				\
	iosassetsrc.h				\
	avfassetsrc.h

if HAVE_IOS

libgstapplemedia_la_SOURCES +=			\
	iosassetsrc.m

libgstapplemedia_la_LDFLAGS +=			\
	-Wl,-framework -Wl,Foundation		\
	-Wl,-framework -Wl,AssetsLibrary

else

libgstapplemedia_la_SOURCES +=			\
	qtkitvideosrc.m

libgstapplemedia_la_LDFLAGS +=			\
	-Wl,-framework -Wl,Cocoa		\
	-Wl,-framework -Wl,QTKit

endif

if HAVE_AVFOUNDATION

libgstapplemedia_la_SOURCES +=			\
	avfvideosrc.m				\
	avfassetsrc.m

libgstapplemedia_la_LDFLAGS +=			\
	-Wl,-framework -Wl,AVFoundation

endif

if HAVE_VIDEOTOOLBOX
libgstapplemedia_la_SOURCES +=			\
	vtenc.c 				\
	vtdec.c

libgstapplemedia_la_LDFLAGS += -Wl,-weak_framework -Wl,VideoToolbox
endif
