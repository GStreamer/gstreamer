if ENABLE_BASH_COMPLETION
bin_PROGRAMS = gst-completion-helper-@GST_API_VERSION@

gst_completion_helper_@GST_API_VERSION@_SOURCES = gst-completion-helper.c
gst_completion_helper_@GST_API_VERSION@_CFLAGS = $(GST_OBJ_CFLAGS)
gst_completion_helper_@GST_API_VERSION@_LDADD = $(GST_OBJ_LIBS)

bashhelpersdir = $(BASH_HELPERS_DIR)
dist_bashhelpers_DATA = gst

install-exec-hook:
	$(MKDIR_P) $(DESTDIR)$(BASH_HELPERS_DIR) && \
	cd $(DESTDIR)$(bindir) && \
	$(INSTALL) `echo "gst-completion-helper-" | sed '$(transform)'`@GST_API_VERSION@$(EXEEXT) \
        $(DESTDIR)$(BASH_HELPERS_DIR)/gst-completion-helper-@GST_API_VERSION@$(EXEEXT) && \
	rm `echo "gst-completion-helper-" | sed '$(transform)'`@GST_API_VERSION@$(EXEEXT)

uninstall-hook:
	rm -f $(DESTDIR)$(BASH_HELPERS_DIR)/gst-completion-helper-@GST_API_VERSION@$(EXEEXT)
endif

helpers_PROGRAMS = gst-plugin-scanner
helpersdir=$(libexecdir)/gstreamer-$(GST_API_VERSION)

gst_plugin_scanner_SOURCES = gst-plugin-scanner.c
gst_plugin_scanner_CFLAGS = $(GST_OBJ_CFLAGS)
gst_plugin_scanner_LDADD = $(GST_OBJ_LIBS)

# clean out the old one to make sure everything is udpated correctly
# remove again after release
CLEANFILES = plugin-scanner

if ENABLE_BASH_COMPLETION
CLEANFILES += gst-completion-helper-@GST_API_VERSION@
endif
