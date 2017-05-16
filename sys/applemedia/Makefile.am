plugin_LTLIBRARIES = libgstapplemedia.la

libgstapplemedia_la_SOURCES =			\
	plugin.m				\
	vtutil.c				\
	corevideomemory.c			\
	corevideobuffer.c			\
	coremediabuffer.c			\
	videotexturecache.m 			\
	atdec.c 				\
	glcontexthelper.c

libgstapplemedia_la_CPPFLAGS =			\
	-Dgst_core_media_buffer_new=gst_core_media_buffer_priv_new		\
	-Dgst_core_media_buffer_get_type=gst_core_media_buffer_priv_get_type	\
	-Dgst_core_media_buffer_get_pixel_buffer=gst_core_media_buffer_priv_get_pixel_buffer\
	-Dgst_core_video_buffer_new=gst_core_video_buffer_priv_new		\
	-Dgst_core_video_buffer_get_type=gst_core_video_buffer_priv_get_type

libgstapplemedia_la_CFLAGS =			\
	-I$(top_srcdir)/gst-libs                \
	-I$(top_builddir)/gst-libs              \
	$(GST_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(GST_PLUGINS_BASE_CFLAGS)

if HAVE_IOS
GST_OBJCFLAGS_WITH_VERSION = $(GST_OBJCFLAGS)
else
# Add min version in front so the user or cerbero can override it since it
# always picks the last occurence when passing several -mmacosx-version-min.
GST_OBJCFLAGS_WITH_VERSION = -mmacosx-version-min=10.8 $(GST_OBJCFLAGS)
endif

libgstapplemedia_la_OBJCFLAGS =			\
	-I$(top_srcdir)/gst-libs                \
	-I$(top_builddir)/gst-libs              \
	-fobjc-arc                              \
	$(GST_OBJCFLAGS_WITH_VERSION)		\
	$(GST_BASE_CFLAGS)			\
	$(GST_PLUGINS_BASE_CFLAGS)

if HAVE_IOS
libgstapplemedia_la_OBJCFLAGS += -fobjc-abi-version=2 -fobjc-legacy-dispatch
endif

libgstapplemedia_la_LIBADD =						\
	$(top_builddir)/gst-libs/gst/gl/libgstgl-$(GST_API_VERSION).la 	\
	$(GST_BASE_LIBS)						\
	$(GST_PLUGINS_BASE_LIBS)					\
	-lgstvideo-$(GST_API_VERSION) 					\
	-lgstaudio-$(GST_API_VERSION)					\
	-lgstpbutils-$(GST_API_VERSION) 				\
	$(GMODULE_NO_EXPORT_LIBS)

libgstapplemedia_la_LIBTOOLFLAGS = --tag=CC

libgstapplemedia_la_LDFLAGS =			\
	$(GST_PLUGIN_LDFLAGS)			\
	$(GST_BASE_LDFLAGS)			\
	$(GST_PLUGINS_BASE_LDFLAGS)		\
	-Wl,-framework -Wl,CoreFoundation	\
	-Wl,-framework -Wl,CoreMedia		\
	-Wl,-framework -Wl,CoreVideo 		\
	-Wl,-framework -Wl,AudioToolbox

noinst_HEADERS =				\
	avfvideosrc.h				\
	avsamplevideosink.h			\
	vtenc.h					\
	vtdec.h					\
	vtutil.h				\
	corevideobuffer.h			\
	coremediabuffer.h			\
	corevideomemory.h			\
	videotexturecache.h			\
	atdec.h 				\
	iosassetsrc.h				\
	iosurfacememory.h			\
	avfassetsrc.h  				\
	glcontexthelper.h 			\
	iosglmemory.h

if HAVE_IOS

libgstapplemedia_la_SOURCES +=			\
	iosassetsrc.m 				\
	iosglmemory.c

libgstapplemedia_la_LDFLAGS +=			\
	-Wl,-framework -Wl,Foundation		\
	-Wl,-framework -Wl,AssetsLibrary

else

libgstapplemedia_la_SOURCES +=			\
	iosurfacememory.c

libgstapplemedia_la_LDFLAGS +=			\
	-Wl,-framework -Wl,Cocoa		\
	-Wl,-framework -Wl,OpenGL 		\
	-Wl,-framework -Wl,IOSurface

endif

if HAVE_AVFOUNDATION

libgstapplemedia_la_SOURCES +=			\
	avfvideosrc.m				\
	avfassetsrc.m				\
	avsamplevideosink.m

libgstapplemedia_la_LDFLAGS +=			\
	-Wl,-framework -Wl,AVFoundation

endif

if HAVE_VIDEOTOOLBOX
libgstapplemedia_la_SOURCES +=			\
	vtenc.c 				\
	vtdec.c

libgstapplemedia_la_LDFLAGS += -Wl,-weak_framework -Wl,VideoToolbox
endif
