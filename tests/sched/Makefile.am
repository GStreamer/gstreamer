if GST_DISABLE_LOADSAVE
noinst_PROGRAMS =
else
noinst_PROGRAMS = runxml dynamic-pipeline
endif

dynamic_pipeline_SOURCES = dynamic-pipeline.c

# nothing but apps here, this is safe
LIBS += $(GST_LIBS)
CFLAGS += $(GST_CFLAGS)
