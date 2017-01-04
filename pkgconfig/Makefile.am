### all of the standard pc files we need to generate
pcverfiles =  \
	gstreamer-allocators-@GST_API_VERSION@.pc \
	gstreamer-audio-@GST_API_VERSION@.pc \
	gstreamer-app-@GST_API_VERSION@.pc \
	gstreamer-fft-@GST_API_VERSION@.pc \
	gstreamer-pbutils-@GST_API_VERSION@.pc \
	gstreamer-riff-@GST_API_VERSION@.pc \
	gstreamer-rtp-@GST_API_VERSION@.pc \
	gstreamer-rtsp-@GST_API_VERSION@.pc \
	gstreamer-sdp-@GST_API_VERSION@.pc \
	gstreamer-tag-@GST_API_VERSION@.pc \
	gstreamer-video-@GST_API_VERSION@.pc \
	gstreamer-plugins-base-@GST_API_VERSION@.pc
pcverfiles_uninstalled = \
	gstreamer-allocators-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-audio-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-app-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-fft-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-pbutils-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-riff-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-rtp-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-rtsp-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-sdp-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-tag-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-video-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-plugins-base-@GST_API_VERSION@-uninstalled.pc

all-local: $(pcverfiles) $(pcverfiles_uninstalled)

cp_verbose = $(cp_verbose_$(V))
cp_verbose_ = $(cp_verbose_$(AM_DEFAULT_VERBOSITY))
cp_verbose_0 = @echo "  CP     $@";

### how to generate versioned .pc files from .pc files in this dir
%-@GST_API_VERSION@.pc: %.pc
	$(cp_verbose_0)cp $< $@
%-@GST_API_VERSION@-uninstalled.pc: %-uninstalled.pc
### the uninstalled libdir is depend of the build system used so set it here
### rather than hardcoding it in the file directly.
	$(AM_V_GEN) sed \
		-e "s|[@]allocatorslibdir[@]|$(abs_top_builddir)/gst-libs/gst/allocators/.libs|" \
		-e "s|[@]audiolibdir[@]|$(abs_top_builddir)/gst-libs/gst/audio/.libs|" \
		-e "s|[@]applibdir[@]|$(abs_top_builddir)/gst-libs/gst/app/.libs|" \
		-e "s|[@]fftlibdir[@]|$(abs_top_builddir)/gst-libs/gst/fft/.libs|" \
		-e "s|[@]pbutilslibdir[@]|$(abs_top_builddir)/gst-libs/gst/pbutils/.libs|" \
		-e "s|[@]rifflibdir[@]|$(abs_top_builddir)/gst-libs/gst/riff/.libs|" \
		-e "s|[@]rtplibdir[@]|$(abs_top_builddir)/gst-libs/gst/rtp/.libs|" \
		-e "s|[@]rtsplibdir[@]|$(abs_top_builddir)/gst-libs/gst/rtsp/.libs|" \
		-e "s|[@]sdplibdir[@]|$(abs_top_builddir)/gst-libs/gst/sdp/.libs|" \
		-e "s|[@]taglibdir[@]|$(abs_top_builddir)/gst-libs/gst/tag/.libs|" \
		-e "s|[@]videolibdir[@]|$(abs_top_builddir)/gst-libs/gst/video/.libs|" \
		$< > $@.tmp && mv $@.tmp $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcverfiles)

CLEANFILES = $(pcverfiles) $(pcverfiles_uninstalled)
pcinfiles = \
		   gstreamer-allocators.pc.in gstreamer-allocators-uninstalled.pc.in \
           gstreamer-audio.pc.in gstreamer-audio-uninstalled.pc.in \
           gstreamer-app.pc.in gstreamer-app-uninstalled.pc.in \
           gstreamer-fft.pc.in gstreamer-fft-uninstalled.pc.in \
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
