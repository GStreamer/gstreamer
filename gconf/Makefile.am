schemadir = @GCONF_SCHEMA_FILE_DIR@
schema_DATA = gstreamer-@GST_MAJORMINOR@.schemas

gstreamer-@GST_MAJORMINOR@.schemas: gstreamer.schemas
	cp gstreamer.schemas gstreamer-@GST_MAJORMINOR@.schemas

install-data-local:
	@GCONF_CONFIG_SOURCE=$(GCONF_SCHEMA_CONFIG_SOURCE) $(GCONFTOOL) \
		--makefile-install-rule $(srcdir)/$(schema_DATA) || \
		(echo ;\
		echo "*****************************************************"; \
		echo "Installation of schemas failed, install them manually"; \
		echo "*****************************************************";) 
	@true

CLEANFILES = $(schema_DATA)
EXTRA_DIST = $(schema_DATA)
