if USE_GCONF
GCONF_PC=gstreamer-gconf-@GST_MAJORMINOR@.pc
GCONF_PC_UNINSTALLED=gstreamer-gconf-@GST_MAJORMINOR@-uninstalled.pc
else
GCONF_PC=
GCONF_PC_UNINSTALLED=
endif

### all of the standard pc files we need to generate
pcverfiles =  \
	$(GCONF_PC) \
	gstreamer-interfaces-@GST_MAJORMINOR@.pc \
	gstreamer-libs-@GST_MAJORMINOR@.pc \
	gstreamer-media-info-@GST_MAJORMINOR@.pc \
	gstreamer-play-@GST_MAJORMINOR@.pc
pcverfiles_uninstalled = \
	$(GCONF_PC_UNINSTALLED) \
	gstreamer-interfaces-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-libs-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-media-info-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-play-@GST_MAJORMINOR@-uninstalled.pc

pcverfiles_gconf = $(GCONF_PC) $(GCONF_PC_UNINSTALLED)
all-local: $(pcverfiles) $(pcverfiles_uninstalled)

### how to generate versioned .pc files from .pc files in this dir
$(pcverfiles): %-@GST_MAJORMINOR@.pc: %.pc
	cp $< $@
$(pcverfiles_uninstalled): %-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = $(pcverfiles)

CLEANFILES = $(pcverfiles) $(pcverfiles_uninstalled)
pcinfiles = \
           gstreamer-gconf.pc.in gstreamer-gconf-uninstalled.pc.in \
           gstreamer-interfaces.pc.in gstreamer-interfaces-uninstalled.pc.in \
           gstreamer-libs.pc.in gstreamer-libs-uninstalled.pc.in \
           gstreamer-media-info.pc.in gstreamer-media-info-uninstalled.pc.in \
           gstreamer-play.pc.in gstreamer-play-uninstalled.pc.in

DISTCLEANFILES = $(pcinfiles:.in=)
EXTRA_DIST = $(pcinfiles)
