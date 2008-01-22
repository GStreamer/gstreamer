GST_DOC_SCANOBJ = $(top_srcdir)/common/gstdoc-scangobj

## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_MAJORMINOR@
MODULE=gst-plugins-bad
DOC_MODULE=$(MODULE)-plugins

# for upload.mak
DOC=$(MODULE)-plugins
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload-doc.mak

# generated basefiles
#basefiles = \
##		$(DOC_MODULE).types \
#		$(DOC_MODULE)-sections.txt \
#		$(DOC_MODULE)-docs.sgml

# ugly hack to make -unused.sgml work
#unused-build.stamp:
#	BUILDDIR=`pwd` && \
#	cd $(srcdir)/tmpl && \
#	ln -sf gstreamer-libs-unused.sgml \
#		$$BUILDDIR/tmpl/gstreamer-libs-@GST_MAJORMINOR@-unused.sgml
#	touch unused-build.stamp

# these rules are added to create parallel docs using GST_MAJORMINOR
#$(basefiles): gstreamer-libs-@GST_MAJORMINOR@%: gstreamer-libs%
#	cp $< $@

#CLEANFILES = $(basefiles)

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code. Relative to $(top_srcdir).
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR = $(top_srcdir)

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=

# FIXME :
# there's something wrong with gstreamer-sections.txt not being in the dist
# maybe it doesn't resolve; we're adding it below for now
#EXTRA_DIST = gstreamer.types.in gstreamer.hierarchy $(DOC_MODULE)-sections.txt gstreamer-sections.txt $(DOC_MAIN_SGML_FILE)

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --source-suffixes=c,h,cc,m

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GSTPB_PREFIX)/share/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(DOC_SOURCE_DIR)/*/*/*.h
CFILE_GLOB=$(DOC_SOURCE_DIR)/*/*/*.c $(DOC_SOURCE_DIR)/*/*/*.cc

# this is a wingo addition
# thomasvs: another nice wingo addition would be an explanation on why
# this is useful ;)

SCANOBJ_DEPS =

# Header files to ignore when scanning.
IGNORE_HFILES = \
	arch.h \
	speex_resampler.h \
	speex_resampler_wrapper.h \
	fixed_generic.h

IGNORE_CFILES =

# we add all .h files of elements that have signals/args we want
# sadly this also pulls in the private methods - maybe we should
# move those around in the source ?
#
# also, we should add some stuff here conditionally based on whether
# or not the plugin will actually build
# but I'm not sure about that - it might be this Just Works given that
# the registry won't have the element
# -> it just works (TM) (ensonic)

# FIXME: not ported yet
#	$(top_srcdir)/ext/gnomevfs/gstgnomevfssink.c 

# example code that needs to be converted to xml and placed in xml/
EXAMPLE_CFILES = \
        $(top_srcdir)/ext/directfb/dfb-example.c

EXTRA_HFILES = \
	$(top_srcdir)/ext/amrwb/gstamrwbdec.h \
	$(top_srcdir)/ext/amrwb/gstamrwbenc.h \
	$(top_srcdir)/ext/amrwb/gstamrwbparse.h \
	$(top_srcdir)/ext/dc1394/gstdc1394.h \
	$(top_srcdir)/ext/directfb/dfbvideosink.h \
	$(top_srcdir)/ext/gio/gstgiosink.h \
	$(top_srcdir)/ext/gio/gstgiosrc.h \
	$(top_srcdir)/ext/gio/gstgiostreamsink.h \
	$(top_srcdir)/ext/gio/gstgiostreamsrc.h \
	$(top_srcdir)/ext/ivorbis/vorbisdec.h \
	$(top_srcdir)/ext/jack/gstjackaudiosink.h \
	$(top_srcdir)/ext/musicbrainz/gsttrm.h \
	$(top_srcdir)/ext/mythtv/gstmythtvsrc.h \
	$(top_srcdir)/ext/sdl/sdlaudiosink.h \
	$(top_srcdir)/ext/sdl/sdlvideosink.h \
	$(top_srcdir)/ext/theora/theoradec.h \
	$(top_srcdir)/ext/timidity/gsttimidity.h \
	$(top_srcdir)/ext/timidity/gstwildmidi.h \
	$(top_srcdir)/gst/dvdspu/gstdvdspu.h \
	$(top_srcdir)/gst/equalizer/gstiirequalizer3bands.h \
	$(top_srcdir)/gst/equalizer/gstiirequalizer10bands.h \
	$(top_srcdir)/gst/equalizer/gstiirequalizernbands.h \
	$(top_srcdir)/gst/festival/gstfestival.h \
	$(top_srcdir)/gst/filter/gstlpwsinc.h \
	$(top_srcdir)/gst/filter/gstbpwsinc.h \
	$(top_srcdir)/gst/modplug/gstmodplug.h \
	$(top_srcdir)/gst/multifile/gstmultifilesink.h \
	$(top_srcdir)/gst/multifile/gstmultifilesrc.h \
	$(top_srcdir)/gst/nuvdemux/gstnuvdemux.h \
	$(top_srcdir)/gst/rawparse/gstaudioparse.h \
	$(top_srcdir)/gst/rawparse/gstvideoparse.h \
	$(top_srcdir)/gst/replaygain/gstrganalysis.h \
	$(top_srcdir)/gst/replaygain/gstrglimiter.h \
	$(top_srcdir)/gst/replaygain/gstrgvolume.h \
	$(top_srcdir)/gst/rtpmanager/gstrtpbin.h \
	$(top_srcdir)/gst/rtpmanager/gstrtpclient.h \
	$(top_srcdir)/gst/rtpmanager/gstrtpjitterbuffer.h \
	$(top_srcdir)/gst/rtpmanager/gstrtpptdemux.h \
	$(top_srcdir)/gst/rtpmanager/gstrtpsession.h \
	$(top_srcdir)/gst/rtpmanager/gstrtpssrcdemux.h \
	$(top_srcdir)/gst/sdp/gstsdpdemux.h \
	$(top_srcdir)/gst/spectrum/gstspectrum.h \
	$(top_srcdir)/gst/speed/gstspeed.h \
	$(top_srcdir)/gst/speexresample/gstspeexresample.h \
	$(top_srcdir)/gst/videosignal/gstvideoanalyse.h \
	$(top_srcdir)/gst/videosignal/gstvideodetect.h \
	$(top_srcdir)/gst/videosignal/gstvideomark.h \
	$(top_srcdir)/sys/dvb/gstdvbsrc.h \
	$(top_srcdir)/sys/glsink/gstgltestsrc.h

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(GST_BASE_CFLAGS) -I$(top_builddir)
GTKDOC_LIBS = $(SCANOBJ_DEPS) $(GST_BASE_LIBS)

GTKDOC_CC=$(LIBTOOL) --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt
DOC_OVERRIDES =

include $(top_srcdir)/common/gtk-doc-plugins.mak
