noinst_PROGRAMS = test-video test-ogg test-mp4 test-readme \
		  test-launch test-sdp test-uri test-auth test-auth-digest \
		  test-multicast test-multicast2 test-appsrc test-appsrc2 \
		  test-video-rtx test-record test-record-auth \
		  test-netclock test-netclock-client \
		  test-onvif-backchannel test-video-disconnect

#INCLUDES = -I$(top_srcdir) -I$(srcdir)

AM_CFLAGS = $(GST_OBJ_CFLAGS)
LDADD = $(GST_OBJ_LIBS) $(GIO_LIBS)

if HAVE_LIBCGROUP
noinst_PROGRAMS += test-cgroups
LDADD += $(LIBCGROUP_LIBS)
endif

test_appsrc2_CFLAGS = \
	$(AM_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
test_appsrc2_LDADD = \
	$(LDADD) \
	$(GST_PLUGINS_BASE_LIBS) -lgstapp-1.0
test_netclock_CFLAGS = \
	$(AM_CFLAGS) \
	$(GST_NET_CFLAGS)
test_netclock_LDADD = \
	$(LDADD) \
	$(GST_NET_LIBS)

test_netclock_client_CFLAGS = \
	$(AM_CFLAGS) \
	$(GST_NET_CFLAGS)
test_netclock_client_LDADD = \
	$(LDADD) \
	$(GST_NET_LIBS)

test_onvif_backchannel_CFLAGS = \
	$(AM_CFLAGS) \
	$(GST_PLUGINS_BASE_CFLAGS)
test_onvif_backchannel_LDADD = \
	$(LDADD) \
	$(GST_PLUGINS_BASE_LIBS) -lgstrtsp-1.0 -lgstsdp-1.0

