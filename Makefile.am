SUBDIRS = sources sources/glue doc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gstreamer-sharp-1.0.pc

EXTRA_DIST = gstreamer-sharp-1.0.pc.in
DISTCLEANFILES = gstreamer-sharp-1.0.pc

regenerate:
	bindinate --name=gstreamer --regenerate=true --merge-with=GstBase-1.0,GstTag-1.0,GstVideo-1.0,GstAudio-1.0,GstPbutils-1.0,GstApp-1.0,GstController-1.0,GstFft-1.0,GstNet-1.0,GstRiff-1.0,GstRtp-1.0,GstRtsp-1.0,GstSdp-1.0 --gir=Gst-1.0
