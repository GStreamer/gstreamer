SUBDIRS = codecparsers vaapi

gen_headers	= gstutils_version.h
noinst_HEADERS	= $(gen_headers)
EXTRA_DIST	= gstutils_version.h.in

# Extra clean files so that maintainer-clean removes *everything*
MAINTAINERCLEANFILES = Makefile.in $(gen_headers)
