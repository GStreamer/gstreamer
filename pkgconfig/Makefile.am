if USE_GCONF
GCONF_PC=gstreamer-gconf-@GST_MAJORMINOR@.pc
GCONF_PC_UNINSTALLED=gstreamer-gconf-@GST_MAJORMINOR@-uninstalled.pc
else
GCONF_PC=
GCONF_PC_UNINSTALLED=
endif

### all of the standard pc files we need to generate
pcfiles =  \
	gstreamer-libs-@GST_MAJORMINOR@.pc \
	gstreamer-play-@GST_MAJORMINOR@.pc
pcfiles_uninstalled = \
	gstreamer-libs-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-play-@GST_MAJORMINOR@-uninstalled.pc

all-local: $(pcfiles) $(pcfiles_uninstalled) \
	$(GCONF_PC) \
	$(GCONF_PC_UNINSTALLED)

### how to generate pc files
$(pcfiles): %-@GST_MAJORMINOR@.pc: %.pc
	cp $< $@
$(pcfiles_uninstalled): %-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	cp $< $@

gstreamer-gconf-@GST_MAJORMINOR@.pc: \
	$(srcdir)/../gst-libs/gst/gconf/gstreamer-gconf.pc
	cp $< $@
gstreamer-gconf-@GST_MAJORMINOR@-uninstalled.pc: \
	$(srcdir)/../gst-libs/gst/gconf/gstreamer-gconf-uninstalled.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = \
	$(GCONF_PC) \
	gstreamer-libs-@GST_MAJORMINOR@.pc \
	gstreamer-play-@GST_MAJORMINOR@.pc

EXTRA_DIST= \
           gstreamer-libs.pc.in gstreamer-libs-uninstalled.pc.in \
           gstreamer-play.pc.in gstreamer-play-uninstalled.pc.in
