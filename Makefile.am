PROJECTFILES = gstreamer-sharp.sln gstreamer-sharp.csproj

SUBDIRS = sources sources/glue samples doc

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = gstreamer-sharp-1.0.pc

EXTRA_DIST = gstreamer-sharp-1.0.pc.in out/gstreamer-sharp.dll.config gstreamer-sharp.snk $(PROJECTFILES)
DISTCLEANFILES = gstreamer-sharp-1.0.pc

MAINTAINERCLEANFILES = \
	compile \
	INSTALL \
	aclocal.m4 \
	ltmain.sh \
	Makefile.in \
	depcomp \
	missing \
	install-sh \
	configure \
	config.sub \
	config.guess

regenerate:
	bindinate --name=gstreamer --regenerate=true --merge-with=GstApp-1.0,GstAudio-1.0,GstBase-1.0,GstController-1.0,GstFft-1.0,GstMpegts-1.0,GstNet-1.0,GstPbutils-1.0,GstRiff-1.0,GstRtp-1.0,GstRtsp-1.0,GstSdp-1.0,GstTag-1.0,GstVideo-1.0 --gir=Gst-1.0
