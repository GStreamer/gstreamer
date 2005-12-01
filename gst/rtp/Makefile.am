plugin_LTLIBRARIES = libgstrtp.la

libgstrtp_la_SOURCES = \
	gstrtp.c \
	gstrtpdepay.c \
	gstrtpmpadepay.c \
	gstrtpmpapay.c \
	gstrtpg711pay.c \
	gstrtpg711depay.c \
	gstrtpgsmpay.c \
	gstrtpgsmparse.c \
	gstrtpamrdepay.c \
	gstrtpamrpay.c \
	gstrtph263pdepay.c \
	gstrtph263ppay.c \
	gstrtph263pay.c \
	gstasteriskh263.c \
	gstrtpmp4vpay.c \
	gstrtpmp4vdepay.c \
	gstrtpspeexpay.c \
	gstrtpspeexdepay.c
	
#gstrtpL16pay.c gstrtpL16parse.c gstrtpgsmpay.c gstrtpgsmparse.c 

if HAVE_WINSOCK2_H
WINSOCK2_LIBS = -lws2_32
else
WINSOCK2_LIBS =
endif

libgstrtp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
                -lgstrtp-@GST_MAJORMINOR@ $(WINSOCK2_LIBS)
libgstrtp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 

noinst_HEADERS = gstrtpL16pay.h 	\
		 gstrtpL16parse.h 	\
		 gstrtpamrdepay.h 	\
		 gstrtpamrpay.h 	\
		 gstrtpg711pay.h 	\
		 gstrtpg711depay.h 	\
		 gstrtpgsmpay.h 	\
		 gstrtpgsmparse.h 	\
		 gstrtpmpadepay.h 	\
		 gstrtpmpapay.h 	\
		 gstrtph263pdepay.h 	\
		 gstrtph263ppay.h 	\
		 gstrtph263pay.h 	\
		 gstrtpmp4vpay.h	\
		 gstrtpmp4vdepay.h	\
		 gstrtpdepay.h		\
		 gstrtph263pay.h	\
		 gstasteriskh263.h  \
		 gstrtpspeexpay.h	\
		 gstrtpspeexdepay.h
