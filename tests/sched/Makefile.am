if GST_DISABLE_LOADSAVE
noinst_PROGRAMS =
else
noinst_PROGRAMS = runxml dynamic-pipeline
endif

dynamic_pipeline_SOURCES = dynamic-pipeline.c

LDADD = $(GST_LIBS)
AM_CFLAGS = $(GST_CFLAGS)
