# we have to put in a little hack here for integration to work well
# ffmpeg can't be in SUBDIRS, because then automake will try to descend in it
# and find conditionals it doesn't know.

# so,
# - don't put it in SUBDIRS so automake doesn't descend
# - put it in DIST_SUBDIRS so make dist picks up the subtree
# - add an all-local hook so it does get built
# this also satisfies make distcheck

SUBDIRS =
DIST_SUBDIRS = ffmpeg

all-local:
	cd ffmpeg && make
