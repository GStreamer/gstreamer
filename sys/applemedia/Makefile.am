plugin_LTLIBRARIES = libgstapplemedia.la

libgstapplemedia_la_SOURCES =			\
	plugin.m				\
	vth264decbin.c				\
	vth264encbin.c				\
	vtenc.c					\
	vtdec.c					\
	vtutil.c				\
	bufferfactory.m				\
	corevideobuffer.c			\
	coremediabuffer.c			\
	coremediactx.c				\
	cvapi.c					\
	cmapi.c					\
	vtapi.c					\
	dynapi.c

libgstapplemedia_la_CFLAGS =			\
	$(GST_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(GST_PLUGINS_BASE_CFLAGS)

libgstapplemedia_la_OBJCFLAGS =			\
	$(GST_CFLAGS)				\
	$(GST_BASE_CFLAGS)			\
	$(GST_PLUGINS_BASE_CFLAGS)

libgstapplemedia_la_LIBADD =			\
	$(GST_BASE_LIBS)			\
	$(GST_PLUGINS_BASE_LIBS)		\
	-lgstinterfaces-$(GST_MAJORMINOR)	\
	-lgstvideo-$(GST_MAJORMINOR)

libgstapplemedia_la_LIBTOOLFLAGS = --tag=disable-static

libgstapplemedia_la_LDFLAGS =			\
	$(GST_PLUGIN_LDFLAGS)			\
	$(GST_BASE_LDFLAGS)			\
	$(GST_PLUGINS_BASE_LDFLAGS)		\
	-Wl,-framework -Wl,CoreFoundation	\
	-Wl,-framework -Wl,CoreVideo

noinst_HEADERS =				\
	qtkitvideosrc.h				\
	miovideosrc.h				\
	miovideodevice.h			\
	avfvideosrc.h				\
	celvideosrc.h				\
	vth264decbin.h				\
	vth264encbin.h				\
	vtenc.h					\
	vtdec.h					\
	vtutil.h				\
	bufferfactory.h				\
	corevideobuffer.h			\
	coremediabuffer.h			\
	coremediactx.h				\
	cvapi.h					\
	cmapi.h					\
	mioapi.h				\
	mtapi.h					\
	celapi.h				\
	vtapi.h					\
	dynapi.h				\
	dynapi-internal.h

if HAVE_IOS

libgstapplemedia_la_SOURCES +=			\
	avfvideosrc.m				\
	celvideosrc.c				\
	mtapi.c					\
	celapi.c

libgstapplemedia_la_LDFLAGS +=			\
	-Wl,-framework -Wl,CoreMedia		\
	-Wl,-framework -Wl,Foundation		\
	-Wl,-framework -Wl,AVFoundation

else

libgstapplemedia_la_SOURCES +=			\
	qtkitvideosrc.m				\
	miovideosrc.c				\
	miovideodevice.c			\
	mioapi.c

libgstapplemedia_la_LDFLAGS +=			\
	-Wl,-framework -Wl,Cocoa		\
	-Wl,-framework -Wl,QTKit

endif

