if BUILD_EXAMPLES
SUBDIRS_EXAMPLES = examples
else
SUBDIRS_EXAMPLES =
endif

if HAVE_CHECK
SUBDIRS_CHECK = check
else
SUBDIRS_CHECK =
endif

SUBDIRS = 			\
	$(SUBDIRS_CHECK)	\
	$(SUBDIRS_EXAMPLES)	\
	icles

DIST_SUBDIRS = 			\
	check			\
	examples		\
	icles
