GST_DOC_SCANOBJ = $(top_srcdir)/common/gstdoc-scangobj

## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_API_VERSION@
MODULE=gst-plugins-bad
DOC_MODULE=$(MODULE)-plugins

# for upload-doc.mak
DOC=$(MODULE)-plugins
FORMATS=html
html: html-build.stamp
include $(top_srcdir)/common/upload-doc.mak

# The top-level SGML file. Change it if you want.
DOC_MAIN_SGML_FILE=$(DOC_MODULE)-docs.sgml

# The directory containing the source code.
# gtk-doc will search all .c & .h files beneath here for inline comments
# documenting functions and macros.
DOC_SOURCE_DIR = $(top_srcdir)/gst $(top_srcdir)/ext $(top_srcdir)/sys

# Extra options to supply to gtkdoc-scan.
SCAN_OPTIONS=

# Extra options to supply to gtkdoc-mkdb.
MKDB_OPTIONS=--sgml-mode --source-suffixes=c,h,cc,m

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GSTPB_PREFIX)/share/gtk-doc/html

# Used for dependencies.
HFILE_GLOB= \
  $(top_srcdir)/gst/*/*.h $(top_srcdir)/ext/*/*.h $(top_srcdir)/sys/*/*.h \
  $(top_srcdir)/ext/*/*.hh
CFILE_GLOB= \
  $(top_srcdir)/gst/*/*.c $(top_srcdir)/ext/*/*.c $(top_srcdir)/sys/*/*.c \
  $(top_srcdir)/ext/*/*.cc $(top_srcdir)/sys/*/*.m

# Header files to ignore when scanning.
IGNORE_HFILES =
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
	$(top_srcdir)/ext/assrender/gstassrender.h \
	$(top_srcdir)/ext/curl/gstcurlbasesink.h \
	$(top_srcdir)/ext/curl/gstcurlfilesink.h \
	$(top_srcdir)/ext/curl/gstcurlftpsink.h \
	$(top_srcdir)/ext/curl/gstcurlhttpsink.h \
	$(top_srcdir)/ext/curl/gstcurlsmtpsink.h \
	$(top_srcdir)/ext/curl/gstcurltlssink.h \
	$(top_srcdir)/ext/dc1394/gstdc1394.h \
	$(top_srcdir)/ext/directfb/dfbvideosink.h \
	$(top_srcdir)/ext/dts/gstdtsdec.h \
	$(top_srcdir)/ext/faac/gstfaac.h \
	$(top_srcdir)/ext/faad/gstfaad.h \
	$(top_srcdir)/ext/kate/gstkateenc.h \
	$(top_srcdir)/ext/kate/gstkatedec.h \
	$(top_srcdir)/ext/kate/gstkateparse.h \
	$(top_srcdir)/ext/kate/gstkatetag.h \
	$(top_srcdir)/ext/kate/gstkatetiger.h \
	$(top_srcdir)/ext/ladspa/gstladspa.h \
	$(top_srcdir)/ext/mimic/gstmimenc.h \
	$(top_srcdir)/ext/mimic/gstmimdec.h \
	$(top_srcdir)/ext/modplug/gstmodplug.h \
	$(top_srcdir)/ext/mpeg2enc/gstmpeg2enc.hh \
	$(top_srcdir)/ext/mplex/gstmplex.hh \
	$(top_srcdir)/ext/mythtv/gstmythtvsrc.h \
	$(top_srcdir)/ext/opencv/gstcvdilate.h \
	$(top_srcdir)/ext/opencv/gstcverode.h \
	$(top_srcdir)/ext/opencv/gstcvequalizehist.h \
	$(top_srcdir)/ext/opencv/gstcvlaplace.h \
	$(top_srcdir)/ext/opencv/gstcvsmooth.h \
	$(top_srcdir)/ext/opencv/gstcvsobel.h \
	$(top_srcdir)/ext/opencv/gstedgedetect.h \
	$(top_srcdir)/ext/opencv/gstfaceblur.h \
	$(top_srcdir)/ext/opencv/gstfacedetect.h \
	$(top_srcdir)/ext/opencv/gstpyramidsegment.h \
	$(top_srcdir)/ext/opencv/gsttemplatematch.h \
	$(top_srcdir)/ext/opencv/gsttextoverlay.h \
	$(top_srcdir)/ext/openni2/gstopenni2src.h \
	$(top_srcdir)/ext/rsvg/gstrsvgdec.h \
	$(top_srcdir)/ext/rsvg/gstrsvgoverlay.h \
	$(top_srcdir)/ext/spandsp/gstspanplc.h \
	$(top_srcdir)/ext/spandsp/gstdtmfdetect.h \
	$(top_srcdir)/ext/sdl/sdlaudiosink.h \
	$(top_srcdir)/ext/sdl/sdlvideosink.h \
	$(top_srcdir)/ext/timidity/gsttimidity.h \
	$(top_srcdir)/ext/timidity/gstwildmidi.h \
	$(top_srcdir)/ext/voaacenc/gstvoaacenc.h \
	$(top_srcdir)/ext/voamrwbenc/gstvoamrwbenc.h \
	$(top_srcdir)/ext/zbar/gstzbar.h \
	$(top_srcdir)/gst/aiff/aiffparse.h \
	$(top_srcdir)/gst/aiff/aiffmux.h \
	$(top_srcdir)/gst/autoconvert/gstautoconvert.h \
	$(top_srcdir)/gst/audiovisualizers/gstaudiovisualizer.h \
	$(top_srcdir)/gst/audiovisualizers/gstspacescope.h \
	$(top_srcdir)/gst/audiovisualizers/gstspectrascope.h \
	$(top_srcdir)/gst/audiovisualizers/gstsynaescope.h \
	$(top_srcdir)/gst/audiovisualizers/gstwavescope.h \
	$(top_srcdir)/gst/camerabin2/gstcamerabin2.h \
	$(top_srcdir)/gst/coloreffects/gstcoloreffects.h \
	$(top_srcdir)/gst/dataurisrc/gstdataurisrc.h \
	$(top_srcdir)/gst/dccp/gstdccpclientsink.h \
	$(top_srcdir)/gst/dccp/gstdccpclientsrc.h \
	$(top_srcdir)/gst/dccp/gstdccpserversink.h \
	$(top_srcdir)/gst/dccp/gstdccpserversrc.h \
	$(top_srcdir)/gst/debugutils/fpsdisplaysink.h \
	$(top_srcdir)/gst/dvdspu/gstdvdspu.h \
	$(top_srcdir)/gst/festival/gstfestival.h \
	$(top_srcdir)/gst/gaudieffects/gstburn.h \
	$(top_srcdir)/gst/gaudieffects/gstchromium.h \
	$(top_srcdir)/gst/gaudieffects/gstdilate.h \
	$(top_srcdir)/gst/gaudieffects/gstdodge.h \
	$(top_srcdir)/gst/gaudieffects/gstexclusion.h \
	$(top_srcdir)/gst/gaudieffects/gstgaussblur.h \
	$(top_srcdir)/gst/gaudieffects/gstplugin.h \
	$(top_srcdir)/gst/gaudieffects/gstsolarize.h \
	$(top_srcdir)/gst/geometrictransform/gstbulge.h \
	$(top_srcdir)/gst/geometrictransform/gstcircle.h \
	$(top_srcdir)/gst/geometrictransform/gstdiffuse.h \
	$(top_srcdir)/gst/geometrictransform/gstfisheye.h \
	$(top_srcdir)/gst/geometrictransform/gstkaleidoscope.h \
	$(top_srcdir)/gst/geometrictransform/gstmarble.h \
	$(top_srcdir)/gst/geometrictransform/gstmirror.h \
	$(top_srcdir)/gst/geometrictransform/gstpinch.h \
	$(top_srcdir)/gst/geometrictransform/gstsphere.h \
	$(top_srcdir)/gst/geometrictransform/gstsquare.h \
	$(top_srcdir)/gst/geometrictransform/gststretch.h \
	$(top_srcdir)/gst/geometrictransform/gsttunnel.h \
	$(top_srcdir)/gst/geometrictransform/gsttwirl.h \
	$(top_srcdir)/gst/geometrictransform/gstwaterripple.h \
	$(top_srcdir)/gst/ivfparse/gstivfparse.h \
	$(top_srcdir)/gst/jpegformat/gstjpegparse.h \
	$(top_srcdir)/gst/liveadder/liveadder.h \
	$(top_srcdir)/gst/mxf/mxfdemux.h \
	$(top_srcdir)/gst/mxf/mxfmux.h \
	$(top_srcdir)/gst/nuvdemux/gstnuvdemux.h \
	$(top_srcdir)/gst/pcapparse/gstpcapparse.h \
	$(top_srcdir)/gst/rawparse/gstaudioparse.h \
	$(top_srcdir)/gst/rawparse/gstvideoparse.h \
	$(top_srcdir)/gst/sdp/gstsdpdemux.h \
	$(top_srcdir)/gst/speed/gstspeed.h \
	$(top_srcdir)/gst/stereo/gststereo.h \
	$(top_srcdir)/gst/videosignal/gstvideoanalyse.h \
	$(top_srcdir)/sys/directdraw/gstdirectdrawsink.h \
	$(top_srcdir)/sys/dvb/gstdvbsrc.h \
	$(top_srcdir)/sys/shm/gstshmsink.h \
	$(top_srcdir)/sys/shm/gstshmsrc.h \
	$(top_srcdir)/gst-libs/gst/interfaces/photography.h

# Images to copy into HTML directory.
HTML_IMAGES = camerabin.png

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = -DGST_USE_UNSTABLE_API $(GST_PLUGINS_BAD_CFLAGS) $(GST_BASE_CFLAGS) -I$(top_builddir)
GTKDOC_LIBS = \
	$(top_builddir)/gst-libs/gst/interfaces/libgstphotography-@GST_API_VERSION@.la \
	$(GST_BASE_LIBS)

GTKDOC_CC=$(LIBTOOL) --tag=CC --mode=compile $(CC)
GTKDOC_LD=$(LIBTOOL) --tag=CC --mode=link $(CC)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt
DOC_OVERRIDES =

include $(top_srcdir)/common/gtk-doc-plugins.mak

SUBDIRS =

FIGURES_SOURCEFILES = camerabin.dot
FIGURES_IMAGES      = camerabin.png

EXTRA_DIST += $(FIGURES_SOURCEFILES)

.dot.png:
	dot -Tpng $< -o$@

update-figures: $(FIGURES_IMAGES)
