if GST_DISABLE_LOADSAVE
noinst_PROGRAMS =
else
noinst_PROGRAMS = runxml dynamic-pipeline sched-stress
endif

dynamic_pipeline_SOURCES = dynamic-pipeline.c 
sched_stress_SOURCES = sched-stress.c
sched_stress_LDADD = $(GST_LIBS) #-lefence

LDADD = $(GST_LIBS)
AM_CFLAGS = $(GST_CFLAGS)
