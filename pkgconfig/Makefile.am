### all of the standard pc files we need to generate
pcverfiles =  \
	gstreamer-audio-@GST_MAJORMINOR@.pc \
	gstreamer-app-@GST_MAJORMINOR@.pc \
	gstreamer-cdda-@GST_MAJORMINOR@.pc \
	gstreamer-fft-@GST_MAJORMINOR@.pc \
	gstreamer-floatcast-@GST_MAJORMINOR@.pc \
	gstreamer-interfaces-@GST_MAJORMINOR@.pc \
	gstreamer-netbuffer-@GST_MAJORMINOR@.pc \
	gstreamer-pbutils-@GST_MAJORMINOR@.pc \
	gstreamer-riff-@GST_MAJORMINOR@.pc \
	gstreamer-rtp-@GST_MAJORMINOR@.pc \
	gstreamer-rtsp-@GST_MAJORMINOR@.pc \
	gstreamer-sdp-@GST_MAJORMINOR@.pc \
	gstreamer-tag-@GST_MAJORMINOR@.pc \
	gstreamer-video-@GST_MAJORMINOR@.pc \
	gstreamer-plugins-base-@GST_MAJORMINOR@.pc
pcverfiles_uninstalled = \
	gstreamer-audio-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-app-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-cdda-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-fft-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-floatcast-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-interfaces-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-netbuffer-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-pbutils-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-riff-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-rtp-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-rtsp-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-sdp-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-tag-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-video-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-plugins-base-@GST_MAJORMINOR@-uninstalled.pc

all-local: $(pcverfiles) $(pcverfiles_uninstalled)

cp_verbose = $(cp_verbose_$(V))
cp_verbose_ = $(cp_verbose_$(AM_DEFAULT_VERBOSITY))
cp_verbose_0 = @echo "  CP     $@";

### how to generate versioned .pc files from .pc files in this dir
%-@GST_MAJORMINOR@.pc: %.pc
	$(cp_verbose_0)cp $< $@
%-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	$(cp_verbose_0)cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcverfiles)

CLEANFILES = $(pcverfiles) $(pcverfiles_uninstalled)
pcinfiles = \
           gstreamer-audio.pc.in gstreamer-audio-uninstalled.pc.in \
           gstreamer-app.pc.in gstreamer-app-uninstalled.pc.in \
           gstreamer-cdda.pc.in gstreamer-cdda-uninstalled.pc.in \
           gstreamer-fft.pc.in gstreamer-fft-uninstalled.pc.in \
           gstreamer-floatcast.pc.in gstreamer-floatcast-uninstalled.pc.in \
           gstreamer-interfaces.pc.in gstreamer-interfaces-uninstalled.pc.in \
           gstreamer-netbuffer.pc.in gstreamer-netbuffer-uninstalled.pc.in \
           gstreamer-pbutils.pc.in gstreamer-pbutils-uninstalled.pc.in \
           gstreamer-riff.pc.in gstreamer-riff-uninstalled.pc.in \
           gstreamer-rtp.pc.in gstreamer-rtp-uninstalled.pc.in \
           gstreamer-rtsp.pc.in gstreamer-rtsp-uninstalled.pc.in \
           gstreamer-sdp.pc.in gstreamer-sdp-uninstalled.pc.in \
           gstreamer-tag.pc.in gstreamer-tag-uninstalled.pc.in \
           gstreamer-video.pc.in gstreamer-video-uninstalled.pc.in \
           gstreamer-plugins-base.pc.in gstreamer-plugins-base-uninstalled.pc.in

DISTCLEANFILES = $(pcinfiles:.in=)
EXTRA_DIST = $(pcinfiles)
