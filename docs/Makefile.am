
if BUILD_PLUGIN_DOCS
SUBDIRS_PLUGINS = plugins
else
SUBDIRS_PLUGINS =
endif

if BUILD_DOCS
SUBDIRS_DOCS = faq manual pwg gst libs
else
SUBDIRS_DOCS =
endif

SUBDIRS = $(SUBDIRS_DOCS) $(SUBDIRS_PLUGINS)
DIST_SUBDIRS = faq manual pwg gst libs plugins xsl

EXTRA_DIST = \
	slides manuals.mak htmlinstall.mak \
	image-png image-pdf image-eps version.entities.in

dist-hook:
	$(RM) -rf $(distdir)/random/CVS
	$(RM) -rf $(distdir)/random/*~
	$(RM) -rf $(distdir)/random/.\#*
	$(RM) -rf $(distdir)/slides/CVS
	$(RM) -rf $(distdir)/slides/*~
	$(RM) -rf $(distdir)/slides/.\#*
