if GST_DISABLE_LOADSAVE
noinst_PROGRAMS =
else
noinst_PROGRAMS = runxml dynamic-pipeline sched-stress interrupt1 interrupt2 interrupt3
endif

dynamic_pipeline_SOURCES = dynamic-pipeline.c 
sched_stress_SOURCES = sched-stress.c
sched_stress_LDADD = $(GST_OBJ_LIBS) #-lefence

LDADD = $(GST_OBJ_LIBS)
AM_CFLAGS = $(GST_OBJ_CFLAGS)
