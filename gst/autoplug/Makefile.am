filterdir = $(libdir)/gst

filter_LTLIBRARIES = libgststaticautoplug.la libgststaticautoplugrender.la

libgststaticautoplug_la_SOURCES =	\
	gststaticautoplug.c 
	
libgststaticautoplugrender_la_SOURCES =	\
	gststaticautoplugrender.c

libgststaticautoplug_la_LDFLAGS = -version-info $(GST_LIBVERSION)
libgststaticautoplugrender_la_LDFLAGS = -version-info $(GST_LIBVERSION)
