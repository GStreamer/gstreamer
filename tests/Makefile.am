if BUILD_EXAMPLES
SUBDIRS_EXAMPLES = examples
else
SUBDIRS_EXAMPLES =
endif

if HAVE_GST_CHECK
SUBDIRS_CHECK = check
else
SUBDIRS_CHECK =
endif

if USE_X
SUBDIRS_ICLES = icles
else
SUBDIRS_ICLES =
endif

SUBDIRS = 			\
	$(SUBDIRS_CHECK)	\
	$(SUBDIRS_EXAMPLES)	\
	$(SUBDIRS_ICLES)

DIST_SUBDIRS = 			\
	check			\
	examples		\
	files			\
	icles
