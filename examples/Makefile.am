if GST_DISABLE_LOADSAVE
GST_LOADSAVE_DIRS = 
else
GST_LOADSAVE_DIRS = xml typefind
endif

if GST_DISABLE_AUTOPLUG
GST_AUTOPLUG_DIRS = 
else
GST_AUTOPLUG_DIRS = autoplug helloworld2
endif

SUBDIRS = $(GST_AUTOPLUG_DIRS) $(GST_LOADSAVE_DIRS) \
          helloworld \
          queue queue2 queue3 queue4 \
          launch thread plugins mixer cutter pingpong manual

DIST_SUBDIRS = autoplug \
          helloworld helloworld2 \
          queue queue2 queue3 queue4 \
          launch thread xml plugins typefind mixer cutter pingpong manual

