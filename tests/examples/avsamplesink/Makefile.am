noinst_PROGRAMS = avsamplesink

avsamplesink_SOURCES = main.m

avsamplesink_OBJCFLAGS= \
	$(GST_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
avsamplesink_LDADD= \
	$(GST_PLUGINS_BASE_LIBS) $(GST_LIBS)
avsamplesink_LDFLAGS =			\
	-Wl,-framework -Wl,AVFoundation \
	-Wl,-framework -Wl,QuartzCore \
	-Wl,-framework -Wl,Cocoa
avsamplesink_LIBTOOLFLAGS = --tag=CC
