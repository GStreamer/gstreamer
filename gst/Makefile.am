# cheap trick to build . first...
SUBDIRS = . types elements autoplug

lib_LTLIBRARIES = libgst.la

if HAVE_CPU_I386
GSTARCH_SRCS = gstcpuid_i386.s
else
GSTARCH_SRCS = 
endif

EXTRA_libgst_la_SOURCES =	\
	gstcpuid_i386.s

if USE_GLIB2
GST_OBJECT_MODEL_SRC = gstmarshal.c
GST_OBJECT_MODEL_HDR = gstmarshal.h
else
GST_OBJECT_MODEL_SRC = gobject2gtk.c
GST_OBJECT_MODEL_HDR = gobject2gtk.h
endif

libgst_la_SOURCES =		\
	cothreads.c		\
	gst.c			\
	$(GST_OBJECT_MODEL_SRC)	\
	gstobject.c		\
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
	gstpad.c		\
	gstpipeline.c		\
	gstplugin.c		\
	gstprops.c		\
	gstqueue.c		\
	gstscheduler.c		\
	gstthread.c		\
	gsttrace.c		\
	gsttype.c		\
	gsttypefind.c		\
	gstutils.c		\
	gstparse.c		\
	$(GSTARCH_SRCS)		\
	gstxml.c


##### Oh this sucks so badly.  This isn't funny. #####

# the compiler shoots cothreads.c in the head at -O6
# FIXME: these manual rules break the dependency generation, so we have a
# nasty hack here.  
#LTCOMPILE2=$(LTCOMPILE) -O2
#COMPILE2=$(COMPILE) -O2

# FIXME this only lists the x86 arch file, we really should merge them all
cothreads.lo: $(srcdir)/cothreads.c $(srcdir)/cothreads.h $(srcdir)/gst_private.h \
  $(top_builddir)/config.h $(srcdir)/gstinfo.c $(srcdir)/gstarch.h $(srcdir)/gsti386.h
	$(LIBTOOL) --mode=compile $(COMPILE) -O2 -c $<
cothreads.o: $(srcdir)/cothreads.c $(srcdir)/cothreads.h $(srcdir)/gst_private.h \
  $(top_builddir)/config.h $(srcdir)/gstinfo.c $(srcdir)/gstarch.h $(srcdir)/gsti386.h
	$(COMPILE) -O2 -c $<

# NOTDEPEND.cothreads.lo: cothreads.c
# NOTDEPEND	$(LTCOMPILE2) -c $<
# NOTDEPEND.cothreads.o: cothreads.c
# NOTDEPEND	$(COMPILE2) -c $<

#cothreads.lo: cothreads.c
#	@echo '$(LTCOMPILE2) -c $<'; \
#	$(LTCOMPILE2) -Wp,-MD,.deps/$(*F).pp -c $<
#	@-sed -e 's/^\([^:]*\)\.o[      ]*:/\1.lo \1.o :/' \
#	  < .deps/$(*F).pp > .deps/$(*F).P; \
#	tr ' ' '\012' < .deps/$(*F).pp \
#	  | sed -e 's/^\\$$//' -e '/^$$/ d' -e '/:$$/ d' -e 's/$$/ :/' \
#	    >> .deps/$(*F).P; \
#	rm -f .deps/$(*F).pp
#cothreads.o: cothreads.c
#	@echo '$(COMPILE2) -c $<'; \
#	$(COMPILE2) -Wp,-MD,.deps/$(*F).pp -c $<
#	@-sed -e 's/^\([^:]*\)\.o[      ]*:/\1.lo \1.o :/' \
#	  < .deps/$(*F).pp > .deps/$(*F).P; \
#	tr ' ' '\012' < .deps/$(*F).pp \
#	  | sed -e 's/^\\$$//' -e '/^$$/ d' -e '/:$$/ d' -e 's/$$/ :/' \
#	    >> .deps/$(*F).P; \
#	rm -f .deps/$(*F).pp

##### end immense suckage #####


libgstincludedir = $(includedir)/gst
libgstinclude_HEADERS =		\
	cothreads.h		\
	gst.h			\
	$(GST_OBJECT_MODEL_HDR)	\
	gstobject.h		\
	gsttypes.h		\
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
	gstpad.h		\
	gstpipeline.h		\
	gstplugin.h		\
	gstprops.h		\
	gstqueue.h		\
	gstscheduler.h		\
	gstthread.h		\
	gsttrace.h		\
	gsttype.h		\
	gsttypefind.h		\
	gstutils.h		\
	gstparse.h		\
	gstversion.h		\
	gstxml.h

noinst_HEADERS = 	\
	gst_private.h	\
	gstarch.h	\
	gsti386.h	\
	gstppc.h	\
	gstalpha.h	\
	gstarm.h	\
	gstsparc.h	\
	gstpropsprivate.h

CFLAGS = $(LIBGST_CFLAGS) -D_GNU_SOURCE
LIBS = $(LIBGST_LIBS)
libgst_la_LDFLAGS = -version-info $(GST_LIBVERSION)

EXTRA_DIST = ROADMAP
