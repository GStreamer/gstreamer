helpers_PROGRAMS = gst-plugin-scanner
helpersdir=$(libexecdir)/gstreamer-$(GST_API_VERSION)

gst_plugin_scanner_SOURCES = gst-plugin-scanner.c
gst_plugin_scanner_CFLAGS = $(GST_OBJ_CFLAGS)
gst_plugin_scanner_LDADD = $(GST_OBJ_LIBS)

if ENABLE_BASH_COMPLETION
helpers_PROGRAMS += gst-completion-helper
gst_completion_helper_SOURCES = gst-completion-helper.c
gst_completion_helper_CFLAGS = $(GST_OBJ_CFLAGS)
gst_completion_helper_LDADD = $(GST_OBJ_LIBS)
endif

if HAVE_PTP
helpers_PROGRAMS += gst-ptp-helper
gst_ptp_helper_SOURCES = gst-ptp-helper.c
gst_ptp_helper_CFLAGS = $(GST_OBJ_CFLAGS) $(GIO_CFLAGS)
gst_ptp_helper_LDADD = $(GST_OBJ_LIBS) $(GIO_LIBS) $(CAP_LIBS)
endif

install-data-hook:
if HAVE_PTP
if HAVE_PTP_HELPER_SETUID
	- chown root $(DESTDIR)$(helpersdir)/gst-ptp-helper
	- chmod u+s $(DESTDIR)$(helpersdir)/gst-ptp-helper
endif
if HAVE_PTP_HELPER_CAPABILITIES
	- $(SETCAP) cap_net_bind_service,cap_net_admin+ep $(DESTDIR)$(helpersdir)/gst-ptp-helper
endif
endif

EXTRA_DIST = ptp_helper_post_install.sh
