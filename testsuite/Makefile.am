include Rules


if GST_DISABLE_PARSE
GST_PARSE_DIRS = 
else
GST_PARSE_DIRS = parse
endif

if GST_DISABLE_GST_DEBUG
GST_DEBUG_DIRS = 
else
GST_DEBUG_DIRS = debug
endif

SUBDIRS = \
	caps \
	$(GST_DEBUG_DIRS) \
	dlopen \
	elements indexers \
	$(GST_PARSE_DIRS) \
	plugin refcounting states threads

DIST_SUBDIRS = \
	caps \
	debug \
	dlopen \
	elements indexers \
	parse \
	plugin refcounting states threads

tests_pass =
tests_fail = 
tests_ignore =

# we makes them, we gots to clean them
CLEANFILES += test-registry.xml

