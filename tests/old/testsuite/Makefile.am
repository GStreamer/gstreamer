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
	bins bytestream caps cleanup clock \
	$(GST_DEBUG_DIRS) \
	dlopen dynparams \
	elements ghostpads indexers negotiation \
	$(GST_PARSE_DIRS) \
	plugin refcounting schedulers states tags threads

DIST_SUBDIRS = \
	bins bytestream caps cleanup clock \
	debug \
	dlopen dynparams \
	elements ghostpads indexers negotiation \
	parse \
	plugin refcounting schedulers states tags threads

tests_pass = test_gst_init
tests_fail = 
tests_ignore =

# we makes them, we gots to clean them
CLEANFILES += test-registry.xml

