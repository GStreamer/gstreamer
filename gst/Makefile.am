# cheap trick to build . first...
SUBDIRS = . types meta elements

lib_LTLIBRARIES = libgst.la

GSTOBJECT_SRCS =	\
	gstobject.c
GSTOBJECT_INCLUDES =	\
	gstobject.h

if HAVE_CPU_I386
GSTARCH_SRCS = gstcpuid_i386.s
else
GSTARCH_SRCS = 
endif

EXTRA_libgst_la_SOURCES =	\
	gstcpuid_i386.s

libgst_la_SOURCES =	\
	gst.c		\
	$(GSTOBJECT_SRCS)	\
	gstpad.c	\
	gstbuffer.c	\
	gstbufferpool.c	\
	gstclock.c	\
	gstcpu.c	\
	$(GSTARCH_SRCS) \
	gstelement.c	\
	gstelementfactory.c	\
	gstextratypes.c	\
	gstbin.c	\
	gstpipeline.c	\
	gstthread.c	\
	gstsrc.c	\
	gstfilter.c	\
	gstsink.c	\
	gstconnection.c	\
	gsttype.c	\
	gstcaps.c	\
	gstplugin.c	\
	gstutils.c	\
	gsttrace.c	\
	gstmeta.c	\
	gsttee.c	\
	gstxml.c	\
	cothreads.c

libgstincludedir = $(includedir)/gst
libgstinclude_HEADERS =	\
	gst.h		\
	gstlog.h	\
	$(GSTOBJECT_INCLUDES)	\
	gstpad.h	\
	gstbuffer.h	\
	gstbufferpool.h	\
	gstclock.h	\
	gstcpu.h	\
	gstelement.h	\
	gstextratypes.h	\
	gstbin.h	\
	gstpipeline.h	\
	gstthread.h	\
	gstsrc.h	\
	gstfilter.h	\
	gstsink.h	\
	gstconnection.h	\
	gsttype.h	\
	gstcaps.h	\
	gstplugin.h	\
	gstutils.h	\
	gsttrace.h	\
	gstmeta.h	\
	gsttee.h	\
	gstxml.h	\
	gstdebug.h	\
	cothreads.h

noinst_HEADERS =	\
	gstarch.h       \
	gsti386.h       \
	gstppc.h        

#CFLAGS += -O2 -Wall 

libgst_la_LIBADD = $(GLIB_LIBS) $(GTK_LIBS) $(XML_LIBS)
libgst_la_LDFLAGS = -version-info $(STREAMER_CURRENT):$(STREAMER_REVISION):$(STREAMER_AGE)

EXTRA_DIST = ROADMAP
