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
	bytestream caps cleanup controller \
	$(GST_DEBUG_DIRS) \
	dlopen \
	elements indexers negotiation pad \
	$(GST_PARSE_DIRS) \
	plugin refcounting schedulers states threads trigger

DIST_SUBDIRS = \
	bytestream caps cleanup controller \
	debug \
	dlopen \
	elements indexers negotiation pad \
	parse \
	plugin refcounting schedulers states threads trigger

tests_pass =
tests_fail = 
tests_ignore =

# we makes them, we gots to clean them
CLEANFILES += test-registry.xml

