
### all of the standard pc files we need to generate
pcverfiles =  \
	gstreamer-plugins-bad-@GST_API_VERSION@.pc \
	gstreamer-codecparsers-@GST_API_VERSION@.pc \
	gstreamer-insertbin-@GST_API_VERSION@.pc \
	gstreamer-mpegts-@GST_API_VERSION@.pc \
	gstreamer-player-@GST_API_VERSION@.pc \
	gstreamer-sctp-@GST_API_VERSION@.pc \
	gstreamer-webrtc-@GST_API_VERSION@.pc \
	gstreamer-bad-audio-@GST_API_VERSION@.pc \
	gstreamer-bad-video-@GST_API_VERSION@.pc

pcverfiles_uninstalled = \
	gstreamer-plugins-bad-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-codecparsers-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-insertbin-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-mpegts-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-player-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-sctp-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-webrtc-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-bad-audio-@GST_API_VERSION@-uninstalled.pc \
	gstreamer-bad-video-@GST_API_VERSION@-uninstalled.pc

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
		-e "s|[@]audiolibdir[@]|$(abs_top_builddir)/gst-libs/gst/audio/.libs|" \
		-e "s|[@]videolibdir[@]|$(abs_top_builddir)/gst-libs/gst/video/.libs|" \
		-e "s|[@]codecparserslibdir[@]|$(abs_top_builddir)/gst-libs/gst/codecparsers/.libs|" \
		-e "s|[@]insertbinlibdir[@]|$(abs_top_builddir)/gst-libs/gst/insertbin/.libs|" \
		-e "s|[@]mpegtslibdir[@]|$(abs_top_builddir)/gst-libs/gst/mpegts/.libs|" \
		-e "s|[@]playerlibdir[@]|$(abs_top_builddir)/gst-libs/gst/player/.libs|" \
		-e "s|[@]waylandlibdir[@]|$(abs_top_builddir)/gst-libs/gst/wayland/.libs|" \
		-e "s|[@]webrtclibdir[@]|$(abs_top_builddir)/gst-libs/gst/webrtc/.libs|" \
		-e "s|[@]basecamerabinsrclibdir[@]|$(abs_top_builddir)/gst-libs/gst/basecamerabinsrc/.libs|" \
		-e "s|[@]photographylibdir[@]|$(abs_top_builddir)/gst-libs/gst/interfaces/.libs|" \
		$< > $@.tmp && mv $@.tmp $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcverfiles)

CLEANFILES = $(pcverfiles) $(pcverfiles_uninstalled)
pcinfiles = \
           gstreamer-plugins-bad.pc.in gstreamer-plugins-bad-uninstalled.pc.in \
           gstreamer-codecparsers.pc.in gstreamer-codecparsers-uninstalled.pc.in \
           gstreamer-insertbin.pc.in gstreamer-insertbin-uninstalled.pc.in \
           gstreamer-mpegts.pc.in gstreamer-mpegts-uninstalled.pc.in \
           gstreamer-player.pc.in gstreamer-player-uninstalled.pc.in \
           gstreamer-webrtc.pc.in gstreamer-webrtc-uninstalled.pc.in \
           gstreamer-bad-audio.pc.in gstreamer-bad-audio-uninstalled.pc.in \
           gstreamer-bad-video.pc.in gstreamer-bad-video-uninstalled.pc.in \
           gstreamer-sctp.pc.in gstreamer-sctp-uninstalled.pc.in

DISTCLEANFILES = $(pcinfiles:.in=)
EXTRA_DIST = $(pcinfiles)
