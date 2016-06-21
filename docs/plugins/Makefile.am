GST_DOC_SCANOBJ = $(top_srcdir)/common/gstdoc-scangobj

## Process this file with automake to produce Makefile.in

# The name of the module, e.g. 'glib'.
#DOC_MODULE=gst-plugins-libs-@GST_API_VERSION@
MODULE=gst-plugins-good
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
MKDB_OPTIONS=--sgml-mode --source-suffixes=c,h,cc

# Extra options to supply to gtkdoc-fixref.
FIXXREF_OPTIONS=--extra-dir=$(GLIB_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GST_PREFIX)/share/gtk-doc/html \
	--extra-dir=$(GSTPB_PREFIX)/share/gtk-doc/html

# Used for dependencies.
HFILE_GLOB= \
  $(top_srcdir)/gst/*/*.h $(top_srcdir)/ext/*/*.h $(top_srcdir)/sys/*/*.h
CFILE_GLOB= \
  $(top_srcdir)/gst/*/*.c $(top_srcdir)/ext/*/*.c $(top_srcdir)/sys/*/*.c \
  $(top_srcdir)/ext/*/*.cc $(top_srcdir)/sys/*/*.m

# Header files to ignore when scanning.
IGNORE_HFILES = 
IGNORE_CFILES =

# we add all .h files of elements that have signals/args we want
# sadly this also pulls in the private methods - maybe we should
# move those around in the source ?
# also, we should add some stuff here conditionally based on whether
# or not the plugin will actually build
# but I'm not sure about that - it might be this Just Works given that
# the registry won't have the element

EXTRA_HFILES = \
	$(top_srcdir)/ext/aalib/gstaasink.h \
	$(top_srcdir)/ext/cairo/gstcairooverlay.h \
	$(top_srcdir)/ext/dv/gstdvdec.h \
	$(top_srcdir)/ext/dv/gstdvdemux.h \
	$(top_srcdir)/ext/flac/gstflacdec.h \
	$(top_srcdir)/ext/flac/gstflacenc.h \
	$(top_srcdir)/ext/flac/gstflactag.h \
	$(top_srcdir)/ext/gdk_pixbuf/gstgdkpixbufsink.h \
	$(top_srcdir)/ext/jack/gstjackaudiosrc.h \
	$(top_srcdir)/ext/jack/gstjackaudiosink.h \
	$(top_srcdir)/ext/jpeg/gstjpegdec.h \
	$(top_srcdir)/ext/jpeg/gstjpegenc.h \
	$(top_srcdir)/ext/libcaca/gstcacasink.h \
	$(top_srcdir)/ext/libpng/gstpngdec.h \
	$(top_srcdir)/ext/libpng/gstpngenc.h \
	$(top_srcdir)/ext/raw1394/gstdv1394src.h \
	$(top_srcdir)/ext/raw1394/gsthdv1394src.h \
	$(top_srcdir)/ext/shout2/gstshout2.h \
	$(top_srcdir)/ext/soup/gstsouphttpsrc.h \
	$(top_srcdir)/ext/taglib/gstapev2mux.h \
	$(top_srcdir)/ext/taglib/gstid3v2mux.h \
	$(top_srcdir)/ext/pulse/pulsesink.h \
	$(top_srcdir)/ext/pulse/pulsesrc.h \
	$(top_srcdir)/ext/speex/gstspeexenc.h \
	$(top_srcdir)/ext/speex/gstspeexdec.h \
	$(top_srcdir)/ext/vpx/gstvp8dec.h \
	$(top_srcdir)/ext/vpx/gstvp8enc.h \
	$(top_srcdir)/ext/wavpack/gstwavpackdec.h \
	$(top_srcdir)/ext/wavpack/gstwavpackenc.h \
	$(top_srcdir)/gst/alpha/gstalpha.h \
	$(top_srcdir)/gst/alpha/gstalphacolor.h \
	$(top_srcdir)/gst/apetag/gstapedemux.h \
	$(top_srcdir)/gst/audiofx/audioamplify.h \
	$(top_srcdir)/gst/audiofx/audioecho.h \
	$(top_srcdir)/gst/audiofx/audiodynamic.h \
	$(top_srcdir)/gst/audiofx/audioinvert.h \
	$(top_srcdir)/gst/audiofx/audiokaraoke.h \
	$(top_srcdir)/gst/audiofx/audiopanorama.h \
	$(top_srcdir)/gst/audiofx/audiocheblimit.h \
	$(top_srcdir)/gst/audiofx/audiochebband.h \
	$(top_srcdir)/gst/audiofx/audioiirfilter.h \
	$(top_srcdir)/gst/audiofx/audiowsincband.h \
	$(top_srcdir)/gst/audiofx/audiowsinclimit.h \
	$(top_srcdir)/gst/audiofx/audiofirfilter.h \
	$(top_srcdir)/gst/audiofx/gstscaletempo.h \
	$(top_srcdir)/gst/audioparsers/gstaacparse.h \
	$(top_srcdir)/gst/audioparsers/gstac3parse.h \
	$(top_srcdir)/gst/audioparsers/gstamrparse.h \
	$(top_srcdir)/gst/audioparsers/gstflacparse.h \
	$(top_srcdir)/gst/audioparsers/gstdcaparse.h \
	$(top_srcdir)/gst/audioparsers/gstmpegaudioparse.h \
	$(top_srcdir)/gst/auparse/gstauparse.h \
	$(top_srcdir)/gst/autodetect/gstautoaudiosink.h \
	$(top_srcdir)/gst/autodetect/gstautoaudiosrc.h \
	$(top_srcdir)/gst/autodetect/gstautovideosink.h \
	$(top_srcdir)/gst/autodetect/gstautovideosrc.h \
	$(top_srcdir)/gst/avi/gstavidemux.h \
	$(top_srcdir)/gst/avi/gstavimux.h \
	$(top_srcdir)/gst/avi/gstavisubtitle.h \
	$(top_srcdir)/gst/cutter/gstcutter.h \
	$(top_srcdir)/gst/debugutils/gstcapssetter.h \
	$(top_srcdir)/gst/debugutils/gsttaginject.h \
	$(top_srcdir)/gst/debugutils/progressreport.h \
	$(top_srcdir)/gst/deinterlace/gstdeinterlace.h \
	$(top_srcdir)/gst/dtmf/gstdtmfsrc.h \
	$(top_srcdir)/gst/dtmf/gstrtpdtmfsrc.h \
	$(top_srcdir)/gst/dtmf/gstrtpdtmfdepay.h \
	$(top_srcdir)/gst/effectv/gstaging.h \
	$(top_srcdir)/gst/effectv/gstdice.h \
	$(top_srcdir)/gst/effectv/gstedge.h \
	$(top_srcdir)/gst/effectv/gstquark.h \
	$(top_srcdir)/gst/effectv/gstrev.h \
	$(top_srcdir)/gst/effectv/gstshagadelic.h \
	$(top_srcdir)/gst/effectv/gstvertigo.h \
	$(top_srcdir)/gst/effectv/gstwarp.h \
	$(top_srcdir)/gst/effectv/gststreak.h \
	$(top_srcdir)/gst/effectv/gstripple.h \
	$(top_srcdir)/gst/effectv/gstop.h \
	$(top_srcdir)/gst/effectv/gstradioac.h \
	$(top_srcdir)/gst/equalizer/gstiirequalizer.h \
	$(top_srcdir)/gst/equalizer/gstiirequalizer3bands.h \
	$(top_srcdir)/gst/equalizer/gstiirequalizer10bands.h \
	$(top_srcdir)/gst/equalizer/gstiirequalizernbands.h \
	$(top_srcdir)/gst/flv/gstflvdemux.h \
	$(top_srcdir)/gst/flv/gstflvmux.h \
	$(top_srcdir)/gst/flx/gstflxdec.h \
	$(top_srcdir)/gst/goom/gstgoom.h \
	$(top_srcdir)/gst/goom2k1/gstgoom.h \
	$(top_srcdir)/gst/law/alaw-decode.h \
	$(top_srcdir)/gst/law/alaw-encode.h \
	$(top_srcdir)/gst/law/mulaw-decode.h \
	$(top_srcdir)/gst/law/mulaw-encode.h \
	$(top_srcdir)/gst/icydemux/gsticydemux.h \
	$(top_srcdir)/gst/id3demux/gstid3demux.h \
	$(top_srcdir)/gst/imagefreeze/gstimagefreeze.h \
	$(top_srcdir)/gst/interleave/deinterleave.h \
	$(top_srcdir)/gst/interleave/interleave.h \
	$(top_srcdir)/gst/level/gstlevel.h \
	$(top_srcdir)/gst/matroska/matroska-demux.h \
	$(top_srcdir)/gst/matroska/matroska-mux.h \
	$(top_srcdir)/gst/matroska/webm-mux.h \
	$(top_srcdir)/gst/monoscope/gstmonoscope.h \
	$(top_srcdir)/gst/multifile/gstmultifilesink.h \
	$(top_srcdir)/gst/multifile/gstmultifilesrc.h \
	$(top_srcdir)/gst/multifile/gstsplitfilesrc.h \
	$(top_srcdir)/gst/multifile/gstsplitmuxsrc.h \
	$(top_srcdir)/gst/multifile/gstsplitmuxsink.h \
	$(top_srcdir)/gst/multipart/multipartdemux.h \
	$(top_srcdir)/gst/multipart/multipartmux.h \
	$(top_srcdir)/gst/isomp4/qtdemux.h \
	$(top_srcdir)/gst/isomp4/gstqtmux.h \
	$(top_srcdir)/gst/isomp4/gstqtmux-doc.h \
	$(top_srcdir)/gst/replaygain/gstrganalysis.h \
	$(top_srcdir)/gst/replaygain/gstrglimiter.h \
	$(top_srcdir)/gst/replaygain/gstrgvolume.h \
	$(top_srcdir)/gst/rtp/gstrtpj2kpay.h \
	$(top_srcdir)/gst/rtp/gstrtpjpegpay.h \
	$(top_srcdir)/gst/rtpmanager/gstrtpbin.h \
	$(top_srcdir)/gst/rtpmanager/gstrtpjitterbuffer.h \
	$(top_srcdir)/gst/rtpmanager/gstrtpptdemux.h \
	$(top_srcdir)/gst/rtpmanager/gstrtpsession.h \
	$(top_srcdir)/gst/rtpmanager/gstrtpssrcdemux.h \
	$(top_srcdir)/gst/rtpmanager/gstrtpmux.h \
	$(top_srcdir)/gst/rtpmanager/gstrtpdtmfmux.h \
	$(top_srcdir)/gst/rtpmanager/gstrtprtxsend.h \
	$(top_srcdir)/gst/rtpmanager/gstrtprtxreceive.h \
	$(top_srcdir)/gst/rtsp/gstrtpdec.h \
	$(top_srcdir)/gst/rtsp/gstrtspsrc.h \
	$(top_srcdir)/gst/shapewipe/gstshapewipe.h \
	$(top_srcdir)/gst/smpte/gstsmpte.h \
	$(top_srcdir)/gst/smpte/gstsmptealpha.h \
	$(top_srcdir)/gst/spectrum/gstspectrum.h \
	$(top_srcdir)/gst/udp/gstmultiudpsink.h \
	$(top_srcdir)/gst/udp/gstudpsrc.h \
	$(top_srcdir)/gst/udp/gstudpsink.h \
	$(top_srcdir)/gst/videobox/gstvideobox.h \
	$(top_srcdir)/gst/videocrop/gstvideocrop.h \
	$(top_srcdir)/gst/videocrop/gstaspectratiocrop.h \
	$(top_srcdir)/gst/videofilter/gstgamma.h \
	$(top_srcdir)/gst/videofilter/gstvideobalance.h \
	$(top_srcdir)/gst/videofilter/gstvideoflip.h \
	$(top_srcdir)/gst/videomixer/videomixer2.h \
	$(top_srcdir)/gst/videomixer/videomixer2pad.h \
	$(top_srcdir)/gst/wavenc/gstwavenc.h \
	$(top_srcdir)/gst/wavparse/gstwavparse.h \
	$(top_srcdir)/gst/y4m/gsty4mencode.h \
	$(top_srcdir)/sys/directsound/gstdirectsoundsink.h \
	$(top_srcdir)/sys/oss4/oss4-sink.h \
	$(top_srcdir)/sys/oss4/oss4-source.h \
	$(top_srcdir)/sys/oss/gstosssink.h \
	$(top_srcdir)/sys/oss/gstosssrc.h \
	$(top_srcdir)/sys/osxaudio/gstosxaudiosrc.h \
	$(top_srcdir)/sys/osxaudio/gstosxaudiosink.h \
	$(top_srcdir)/sys/osxvideo/osxvideosink.h \
	$(top_srcdir)/sys/v4l2/gstv4l2src.h \
	$(top_srcdir)/sys/v4l2/gstv4l2sink.h \
	$(top_srcdir)/sys/v4l2/gstv4l2transform.h \
	$(top_srcdir)/sys/v4l2/gstv4l2videodec.h \
	$(top_srcdir)/sys/v4l2/gstv4l2radio.h \
	$(top_srcdir)/sys/waveform/gstwaveformsink.h \
	$(top_srcdir)/sys/ximage/gstximagesrc.h

# example code that needs to be converted to xml and placed in xml/
EXAMPLE_CFILES = \
	$(top_srcdir)/tests/examples/level/level-example.c \
	$(top_srcdir)/tests/examples/spectrum/spectrum-example.c \
	$(top_srcdir)/tests/examples/audiofx/firfilter-example.c \
	$(top_srcdir)/tests/examples/audiofx/iirfilter-example.c

# Images to copy into HTML directory.
HTML_IMAGES =

# Extra SGML files that are included by $(DOC_MAIN_SGML_FILE).
content_files =

# Other files to distribute.
extra_files =

# CFLAGS and LDFLAGS for compiling scan program. Only needed if your app/lib
# contains GtkObjects/GObjects and you want to document signals and properties.
GTKDOC_CFLAGS = $(GST_BASE_CFLAGS) -I$(top_builddir)
GTKDOC_LIBS = $(GST_BASE_LIBS)

# If you need to override some of the declarations, place them in this file
# and uncomment this line.
#DOC_OVERRIDES = $(DOC_MODULE)-overrides.txt
DOC_OVERRIDES =

include $(top_srcdir)/common/gtk-doc-plugins.mak
