# cheap trick to build . first...
SUBDIRS = . types meta elements

lib_LTLIBRARIES = libgst.la

if HAVE_CPU_I386
GSTARCH_SRCS = gstcpuid_i386.s
else
GSTARCH_SRCS = 
endif

EXTRA_libgst_la_SOURCES =	\
	gstcpuid_i386.s

libgst_la_SOURCES =		\
	cothreads.c		\
	gst.c			\
	gstautoplug.c		\
	gstbin.c		\
	gstbuffer.c		\
	gstbufferpool.c		\
	gstcaps.c		\
	gstclock.c		\
	gstcpu.c		\
	gstelement.c		\
	gstelementfactory.c	\
	gstextratypes.c		\
	gstinfo.c		\
	gstmeta.c		\
	gstobject.c		\
	gstpad.c		\
	gstpipeline.c		\
	gstplugin.c		\
	gstprops.c		\
	gstscheduler.c		\
	gsttee.c		\
	gstthread.c		\
	gsttrace.c		\
	gsttype.c		\
	gstutils.c		\
	gstxml.c		\
	gstparse.c		\
	$(GSTARCH_SRCS)

# the compiler shoots cothreads.c in the head at -O6
cothreads.lo: cothreads.c
	$(LTCOMPILE) -O2 -c $<
cothreads.o: cothreads.c
	$(COMPILE) -O2 -c $<

libgstincludedir = $(includedir)/gst
libgstinclude_HEADERS =		\
	cothreads.h		\
	gst.h			\
	gstautoplug.h		\
	gstbin.h		\
	gstbuffer.h		\
	gstbufferpool.h		\
	gstcaps.h		\
	gstclock.h		\
	gstcpu.h		\
	gstelement.h		\
	gstextratypes.h		\
	gstinfo.h		\
	gstlog.h		\
	gstmeta.h		\
	gstobject.h		\
	gstpad.h		\
	gstpipeline.h		\
	gstplugin.h		\
	gstprops.h		\
	gstscheduler.h		\
	gsttee.h		\
	gstthread.h		\
	gsttrace.h		\
	gsttype.h		\
	gstutils.h		\
	gstparse.h		\
	gstxml.h

noinst_HEADERS = 	\
	gst_private.h	\
	gstarch.h	\
	gsti386.h	\
	gstppc.h	\
	gstpropsprivate.h

CFLAGS = $(LIBGST_CFLAGS)
LIBS = $(LIBGST_LIBS)
libgst_la_LDFLAGS = -version-info $(GSTREAMER_LIBVERSION)

EXTRA_DIST = ROADMAP
