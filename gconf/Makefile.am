GST_SCHEMA_FILES = gstreamer-@GST_MAJORMINOR@.schemas

if USE_GCONF
schemadir = @GCONF_SCHEMA_FILE_DIR@
schema_DATA = $(GST_SCHEMA_FILES)
endif

gstreamer-@GST_MAJORMINOR@.schemas: gstreamer.schemas
	cp gstreamer.schemas gstreamer-@GST_MAJORMINOR@.schemas

if USE_GCONF
if GCONF_SCHEMAS_INSTALL
install-data-local:
	@GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) \
		--makefile-install-rule $(builddir)/$(schema_DATA) || \
		(echo ;\
		echo "*****************************************************"; \
		echo "Installation of schemas failed, install them manually"; \
		echo "*****************************************************";) 
	@true
else
install-data-local:
	@echo "***************************************************************"
	@echo "Not installing schemas, disabled with --disable-schemas-install"
	@echo "***************************************************************"
	@true
endif
endif # USE_GCONF

CLEANFILES = $(GST_SCHEMA_FILES)
EXTRA_DIST = $(GST_SCHEMA_FILES)
