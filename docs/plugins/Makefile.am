GST_DOC_SCANOBJ = $(top_srcdir)/common/gstdoc-scangobj

## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_API_VERSION@
MODULE=gst-plugins-base
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
MKDB_OPTIONS=--sgml-mode

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(top_builddir)/docs/libs/html \
	--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(datadir)/gtk-doc/html

# Used for dependencies.
HFILE_GLOB=$(top_srcdir)/gst/*/*.h $(top_srcdir)/ext/*/*.h $(top_srcdir)/sys/*/*.h
CFILE_GLOB=$(top_srcdir)/gst/*/*.c $(top_srcdir)/ext/*/*.c $(top_srcdir)/sys/*/*.c

# Header files to ignore when scanning.
IGNORE_HFILES = avcodec.h dsputil.h arch.h speex_resampler.h speex_resampler_wrapper.h fixed_arm4.h fixed_arm5e.h fixed_bfin.h fixed_debug.h fixed_generic.h resample_sse.h
IGNORE_CFILES = utils.c mem.c imgconvert.c

# we add all .h files of elements that have signals/args we want
# sadly this also pulls in the private methods - maybe we should
# move those around in the source ?
# also, we should add some stuff here conditionally based on whether
# or not the plugin will actually build
# but I'm not sure about that - it might be this Just Works given that
# the registry won't have the element

EXTRA_HFILES = \
	$(top_srcdir)/ext/alsa/gstalsasink.h \
	$(top_srcdir)/ext/alsa/gstalsasrc.h \
	$(top_srcdir)/ext/cdparanoia/gstcdparanoiasrc.h \
	$(top_srcdir)/ext/ogg/gstoggdemux.h \
	$(top_srcdir)/ext/ogg/gstoggmux.h \
	$(top_srcdir)/ext/opus/gstopusdec.h \
	$(top_srcdir)/ext/opus/gstopusenc.h \
	$(top_srcdir)/ext/pango/gstclockoverlay.h \
	$(top_srcdir)/ext/pango/gsttextoverlay.h \
	$(top_srcdir)/ext/pango/gsttextrender.h \
	$(top_srcdir)/ext/pango/gsttimeoverlay.h \
	$(top_srcdir)/ext/theora/gsttheoradec.h \
	$(top_srcdir)/ext/theora/gsttheoraenc.h \
	$(top_srcdir)/ext/theora/gsttheoraparse.h \
	$(top_srcdir)/ext/vorbis/gstvorbisdec.h \
	$(top_srcdir)/ext/vorbis/gstvorbisenc.h \
	$(top_srcdir)/ext/vorbis/gstvorbisparse.h \
	$(top_srcdir)/ext/vorbis/gstvorbistag.h \
	$(top_srcdir)/gst/adder/gstadder.h \
	$(top_srcdir)/gst/audioconvert/gstaudioconvert.h \
	$(top_srcdir)/gst/audiorate/gstaudiorate.h \
	$(top_srcdir)/gst/audioresample/gstaudioresample.h \
	$(top_srcdir)/gst/audiotestsrc/gstaudiotestsrc.h \
	$(top_srcdir)/gst/encoding/gstencodebin.h \
	$(top_srcdir)/gst/gio/gstgiosink.h \
	$(top_srcdir)/gst/gio/gstgiosrc.h \
	$(top_srcdir)/gst/gio/gstgiostreamsink.h \
	$(top_srcdir)/gst/gio/gstgiostreamsrc.h \
	$(top_srcdir)/gst/playback/gstplay-enum.h \
	$(top_srcdir)/gst/playback/gstplaysink.h \
	$(top_srcdir)/gst/playback/gststreamsynchronizer.h \
	$(top_srcdir)/gst/playback/gstsubtitleoverlay.h \
	$(top_srcdir)/gst/audiorate/gstaudiorate.h \
	$(top_srcdir)/gst/audioresample/gstaudioresample.h \
	$(top_srcdir)/gst/subparse/gstssaparse.h \
	$(top_srcdir)/gst/subparse/gstsubparse.h \
	$(top_srcdir)/gst/tcp/gstmultifdsink.h \
	$(top_srcdir)/gst/tcp/gstmultihandlesink.h \
	$(top_srcdir)/gst/tcp/gstmultisocketsink.h \
	$(top_srcdir)/gst/tcp/gstsocketsrc.h \
	$(top_srcdir)/gst/tcp/gsttcpclientsink.h \
	$(top_srcdir)/gst/tcp/gsttcpclientsrc.h \
	$(top_srcdir)/gst/tcp/gsttcp.h \
	$(top_srcdir)/gst/tcp/gsttcpserversink.h \
	$(top_srcdir)/gst/tcp/gsttcpserversrc.h \
	$(top_srcdir)/gst/videoconvert/gstvideoconvert.h \
	$(top_srcdir)/gst/videorate/gstvideorate.h \
	$(top_srcdir)/gst/videoscale/gstvideoscale.h \
	$(top_srcdir)/gst/videotestsrc/gstvideotestsrc.h \
	$(top_srcdir)/gst/volume/gstvolume.h \
	$(top_srcdir)/sys/ximage/ximagesink.h \
	$(top_srcdir)/sys/xvimage/xvimagesink.h \
	$(top_srcdir)/gst-libs/gst/app/gstappsrc.h \
	$(top_srcdir)/gst-libs/gst/app/gstappsink.h

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(GST_BASE_CFLAGS) -I$(top_builddir) -I$(top_builddir)/gst-libs
GTKDOC_LIBS = $(GST_BASE_LIBS)

# If you need to override some of the declarations, place them in this file
DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt

include $(top_srcdir)/common/gtk-doc-plugins.mak
