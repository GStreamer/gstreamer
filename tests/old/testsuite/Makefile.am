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

SUBDIRS = bins bytestream cleanup dynparams \
	caps plugin elements clock refcounting tags threads \
	indexers debug $(GST_PARSE_DIRS) $(GST_DEBUG_DIRS)

DIST_SUBDIRS = bins bytestream caps caps2 cleanup clock dynparams elements indexers \
		plugin refcounting tags threads parse debug

tests_pass = test_gst_init
tests_fail = 

# we makes them, we gots to clean them
CLEANFILES += test-registry.xml

EXTRA_DIST = gst-inspect-check

