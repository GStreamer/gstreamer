# cheap trick to build . first...

lib_LTLIBRARIES = libgst.la

if HAVE_CPU_I386
GSTARCH_SRCS = gstcpuid_i386.s
else
GSTARCH_SRCS = 
endif

#GST_INSTRUMENT_FLAGS = -finstrument-functions -DGST_ENABLE_FUNC_INSTRUMENTATION

if USE_GLIB2
GST_OBJECT_MODEL_SRC = gstmarshal.c
GST_OBJECT_MODEL_HDR = gstmarshal.h
else
GST_OBJECT_MODEL_SRC = gobject2gtk.c
GST_OBJECT_MODEL_HDR = gobject2gtk.h
endif

if GST_DISABLE_LOADSAVE
GST_LOADSAVE_SRC = 
else
GST_LOADSAVE_SRC = gstxml.c
endif

if GST_DISABLE_TYPEFIND
GST_TYPEFIND_SRC = 
else
GST_TYPEFIND_SRC = gsttypefind.c
endif

if GST_DISABLE_PARSE
GST_PARSE_SRC = 
else
GST_PARSE_SRC = gstparse.c
endif

if GST_DISABLE_AUTOPLUG
GST_AUTOPLUG_SRC = 
GST_AUTOPLUG_DIRS =
else
GST_AUTOPLUG_SRC = gstautoplug.c
GST_AUTOPLUG_DIRS = autoplug
endif

if GST_DISABLE_TRACE
GST_TRACE_SRC = 
else
GST_TRACE_SRC = gsttrace.c
endif

EXTRA_libgst_la_SOURCES = gstcpuid_i386.s gstmarshal.list gobject2gtk.c gobject2gtk.h gstxml.c gsttypefind.c gstparse.c gstautoplug.c gsttrace.c

SUBDIRS = . types elements $(GST_AUTOPLUG_DIRS)
DIST_SUBDIRS = types elements autoplug

libgst_la_SOURCES =		\
	cothreads.c		\
	gst.c			\
	$(GST_OBJECT_MODEL_SRC)	\
	gstobject.c		\
	$(GST_AUTOPLUG_SRC)	\
	gstbin.c		\
	gstbuffer.c		\
	gstbufferpool.c		\
	gstcaps.c		\
	gstclock.c		\
	gstcpu.c		\
	gstdparam.c		\
	gstdparammanager.c	\
	gstelement.c		\
	gstelementfactory.c	\
	gstevent.c		\
	gstextratypes.c		\
	gstinfo.c		\
	gstpad.c		\
	gstpipeline.c		\
	gstplugin.c		\
	gstpluginfeature.c	\
	gstprops.c		\
	gstqueue.c		\
	gstscheduler.c		\
	gstthread.c		\
	$(GST_TRACE_SRC)	\
	gsttype.c		\
	$(GST_TYPEFIND_SRC)	\
	gstutils.c		\
	gsttimecache.c		\
	$(GST_PARSE_SRC)	\
	$(GSTARCH_SRCS)		\
	$(GST_LOADSAVE_SRC)


##### Oh this sucks so badly.  This isn't funny. #####

# the compiler shoots cothreads.c in the head at -O6
# FIXME: these manual rules break the dependency generation, so we have a
# nasty hack here.  
#LTCOMPILE2=$(LTCOMPILE) -O2
#COMPILE2=$(COMPILE) -O2

# FIXME this only lists the x86 arch file, we really should merge them all
cothreads.lo: $(srcdir)/cothreads.c $(srcdir)/cothreads.h $(srcdir)/gst_private.h \
  $(top_builddir)/config.h $(srcdir)/gstinfo.c $(srcdir)/gstarch.h
	$(LIBTOOL) --mode=compile $(COMPILE) -O2 -c $<
cothreads.o: $(srcdir)/cothreads.c $(srcdir)/cothreads.h $(srcdir)/gst_private.h \
  $(top_builddir)/config.h $(srcdir)/gstinfo.c $(srcdir)/gstarch.h
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

##### Other built sources #####

# Generate both marshal files together.  Makes dependency work easier.
gstmarshal.h: gstmarshal.list
	glib-genmarshal --header --prefix=gst_marshal gstmarshal.list > gstmarshal.h
	echo "#include \"glib-object.h\"" >gstmarshal.c
	glib-genmarshal --body --prefix=gst_marshal gstmarshal.list >> gstmarshal.c

gstmarshal.c: gstmarshal.list
	glib-genmarshal --header --prefix=gst_marshal gstmarshal.list > gstmarshal.h
	echo "#include \"glib-object.h\"" >gstmarshal.c
	glib-genmarshal --body --prefix=gst_marshal gstmarshal.list >> gstmarshal.c

##### end built sources #####

# Don't want the generated marshal files in the dist
dist-hook:
	rm -f $(distdir)/gstmarshal.c $(distdir)/gstmarshal.h
# Clean generated files
distclean-local:
	rm -f $(builddir)/gstmarshal.c $(builddir)/gstmarshal.h

libgstincludedir = $(includedir)/gst
libgstinclude_HEADERS =		\
	cothreads.h		\
	gst.h			\
	gstconfig.h		\
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
	gstdata.h		\
	gstdparam.h		\
	gstdparammanager.h	\
	gstelement.h		\
	gstevent.h		\
	gstextratypes.h		\
	gstinfo.h		\
	gstlog.h		\
	gstpad.h		\
	gstpipeline.h		\
	gstplugin.h		\
	gstpluginfeature.h	\
	gstprops.h		\
	gstqueue.h		\
	gstscheduler.h		\
	gstthread.h		\
	gsttrace.h		\
	gsttype.h		\
	gsttypefind.h		\
	gstutils.h		\
	gsttimecache.h		\
	gstparse.h		\
	gstversion.h		\
	gstxml.h

noinst_HEADERS = 	\
	gst_private.h	\
	gstarch.h	\
	gstpropsprivate.h

CFLAGS = $(LIBGST_CFLAGS) -D_GNU_SOURCE -DGST_CONFIG_DIR=\""$(GST_CONFIG_DIR)"\" -Wall
LIBS = $(LIBGST_LIBS)
LDFLAGS = ""
libgst_la_LDFLAGS = -version-info $(GST_LIBVERSION)

EXTRA_DIST = ROADMAP
