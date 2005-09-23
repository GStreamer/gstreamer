plugin_LTLIBRARIES = libgstrtp.la

libgstrtp_la_SOURCES = gstrtp.c \
		       gstrtpdec.c \
		       gstrtpmpadec.c \
		       gstrtpmpaenc.c \
		       gstrtpgsmenc.c \
		       gstrtpgsmparse.c \
		       gstrtpamrdec.c \
		       gstrtpamrenc.c \
		       gstrtph263pdec.c \
		       gstrtph263penc.c \
		       gstrtpmp4venc.c	\
		       gstrtpmp4vdec.c

#gstrtpL16enc.c gstrtpL16parse.c gstrtpgsmenc.c gstrtpgsmparse.c 

libgstrtp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
                -lgstrtp-@GST_MAJORMINOR@
libgstrtp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 

noinst_HEADERS = gstrtpL16enc.h 	\
		 gstrtpL16parse.h 	\
		 gstrtpamrdec.h 	\
		 gstrtpamrenc.h 	\
		 gstrtpgsmenc.h 	\
		 gstrtpgsmparse.h 	\
		 gstrtpmpadec.h 	\
		 gstrtpmpaenc.h 	\
		 gstrtph263pdec.h 	\
		 gstrtph263penc.h 	\
		 gstrtpmp4venc.h	\
		 gstrtpmp4vdec.h	\
		 gstrtpdec.h
