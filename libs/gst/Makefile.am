if HAVE_CHECK
SUBDIRS_CHECK = check
else
SUBDIRS_CHECK =
endif

if GST_DISABLE_NET
SUBDIRS_NET =
else
if HAVE_SYS_SOCKET_H
SUBDIRS_NET = net
else
if HAVE_WINSOCK2_H
SUBDIRS_NET = net
else
SUBDIRS_NET =
endif
endif
endif

if GST_DISABLE_REGISTRY
SUBDIRS_HELPERS =
else
SUBDIRS_HELPERS = helpers
endif

SUBDIRS_ALWAYS = base controller dataprotocol

SUBDIRS = $(SUBDIRS_ALWAYS) $(SUBDIRS_CHECK) $(SUBDIRS_NET) $(SUBDIRS_HELPERS)
DIST_SUBDIRS = $(SUBDIRS_ALWAYS) check net helpers

Android.mk: Makefile.am
	echo $(PWD)
	androgenizer -:PROJECT gstreamer \
	 -:SUBDIR $(patsubst %, libs/gst/%, $(SUBDIRS)) \
	> $@
