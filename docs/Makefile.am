
if BUILD_PLUGIN_DOCS
SUBDIRS_PLUGINS = plugins
else
SUBDIRS_PLUGINS =
endif

if BUILD_DOCS
SUBDIRS_DOCS = gst libs
else
SUBDIRS_DOCS =
endif

SUBDIRS = $(SUBDIRS_DOCS) $(SUBDIRS_PLUGINS)
DIST_SUBDIRS = gst faq manual pwg libs plugins xsl

EXTRA_DIST = \
	slides manuals.mak htmlinstall.mak \
	image-png image-pdf image-eps version.entities.in

upload:
	@for a in manual faq pwg; do cd $$a; make upload; cd ..; done

dist-hook:
	$(RM) -rf $(distdir)/random/CVS
	$(RM) -rf $(distdir)/random/*~
	$(RM) -rf $(distdir)/random/.\#*
	$(RM) -rf $(distdir)/slides/CVS
	$(RM) -rf $(distdir)/slides/*~
	$(RM) -rf $(distdir)/slides/.\#*
