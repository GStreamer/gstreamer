if USE_GCONF
GCONF_PC=gstreamer-gconf-@GST_MAJORMINOR@.pc
GCONF_PC_UNINSTALLED=gstreamer-gconf-@GST_MAJORMINOR@-uninstalled.pc
else
GCONF_PC=
GCONF_PC_UNINSTALLED=
endif

### all of the standard pc files we need to generate
pcfiles =  \
	$(GCONF_PC) \
	gstreamer-libs-@GST_MAJORMINOR@.pc \
	gstreamer-play-@GST_MAJORMINOR@.pc \
	gstreamer-interfaces-@GST_MAJORMINOR@.pc
pcfiles_uninstalled = \
	$(GCONF_PC_UNINSTALLED) \
	gstreamer-libs-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-play-@GST_MAJORMINOR@-uninstalled.pc \
	gstreamer-interfaces-@GST_MAJORMINOR@-uninstalled.pc

pcfiles_gconf = $(GCONF_PC) $(GCONF_PC_UNINSTALLED)
all-local: $(pcfiles) $(pcfiles_uninstalled)

### how to generate pc files from .pc files in this dir
$(pcfiles): %-@GST_MAJORMINOR@.pc: %.pc
	cp $< $@
$(pcfiles_uninstalled): %-@GST_MAJORMINOR@-uninstalled.pc: %-uninstalled.pc
	cp $< $@

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = \
	$(GCONF_PC) \
	gstreamer-libs-@GST_MAJORMINOR@.pc \
	gstreamer-play-@GST_MAJORMINOR@.pc \
	gstreamer-interfaces-@GST_MAJORMINOR@.pc

CLEANFILES = $(pcfiles) $(pcfiles_uninstalled)
EXTRA_DIST= \
           gstreamer-gconf.pc.in gstreamer-gconf-uninstalled.pc.in \
           gstreamer-interfaces.pc.in gstreamer-interfaces-uninstalled.pc.in \
           gstreamer-libs.pc.in gstreamer-libs-uninstalled.pc.in \
           gstreamer-play.pc.in gstreamer-play-uninstalled.pc.in
