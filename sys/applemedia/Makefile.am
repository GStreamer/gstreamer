plugin_LTLIBRARIES = libgstapplemedia.la

libgstapplemedia_la_SOURCES =			\
	plugin.m				\
	vtutil.c				\
	bufferfactory.m				\
	corevideobuffer.c			\
	coremediabuffer.c			\
	coremediactx.c				\
	vtapi.c					\
	dynapi.c

libgstapplemedia_la_CFLAGS =			\
	$(GST_CFLAGS)			\
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
	-lgstvideo-$(GST_API_VERSION)

libgstapplemedia_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

libgstapplemedia_la_LDFLAGS =			\
	$(GST_PLUGIN_LDFLAGS)			\
	$(GST_BASE_LDFLAGS)			\
	$(GST_PLUGINS_BASE_LDFLAGS)		\
	-Wl,-framework -Wl,CoreFoundation	\
	-Wl,-framework -Wl,CoreMedia		\
	-Wl,-framework -Wl,CoreVideo

noinst_HEADERS =				\
	qtkitvideosrc.h				\
	avfvideosrc.h				\
	vth264decbin.h				\
	vth264encbin.h				\
	vtenc.h					\
	vtdec.h					\
	vtutil.h				\
	bufferfactory.h				\
	corevideobuffer.h			\
	coremediabuffer.h			\
	coremediactx.h				\
	vtapi.h					\
	dynapi.h				\
	dynapi-internal.h

if HAVE_IOS

libgstapplemedia_la_SOURCES +=			\
	avfvideosrc.m

libgstapplemedia_la_LDFLAGS +=			\
	-Wl,-framework -Wl,CoreMedia		\
	-Wl,-framework -Wl,Foundation		\
	-Wl,-framework -Wl,AVFoundation

else

libgstapplemedia_la_SOURCES +=			\
	qtkitvideosrc.m				\
	vtenc.c 				\
	vtdec.c

libgstapplemedia_la_LDFLAGS +=			\
	-Wl,-framework -Wl,Cocoa		\
	-Wl,-framework -Wl,QTKit

endif

