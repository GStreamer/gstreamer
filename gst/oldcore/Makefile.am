plugin_LTLIBRARIES = libgsstandardelements.la

libgststandardelements_la_SOURCES =	\
	gstelements.c			\
	gstaggregator.c			\
	gstfdsink.c			\
	gstfdsrc.c			\
	gstmd5sink.c			\
	gstmultifilesrc.c		\
	gstpipefilter.c			\
	gstshaper.c			\
	gststatistics.c

libgststandardelements_la_CFLAGS = $(GST_CFLAGS)
libgststandardelements_la_LIBADD = $(GST_LIBS)
libgststandardelements_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)
libgststandardelements_la_LIBTOOLFLAGS = --tag=disable-static

noinst_HEADERS =		\
	gstaggregator.h		\
	gstfdsink.h		\
	gstfdsrc.h		\
	gstmd5sink.h		\
	gstmultifilesrc.h	\
	gstpipefilter.h		\
	gstshaper.h		\
	gststatistics.h
