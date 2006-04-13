plugin_LTLIBRARIES = libgstrtp.la

libgstrtp_la_SOURCES = \
	gstrtp.c \
	gstrtpdepay.c \
	gstrtpmpadepay.c \
	gstrtpmpapay.c \
	gstrtppcmadepay.c \
	gstrtppcmudepay.c \
	gstrtppcmupay.c \
	gstrtppcmapay.c \
	gstrtpgsmdepay.c \
	gstrtpgsmpay.c \
	gstrtpamrdepay.c \
	gstrtpamrpay.c \
	gstrtph263pdepay.c \
	gstrtph263ppay.c \
	gstrtph263pay.c \
	gstrtpilbcpay.c \
	gstrtpilbcdepay.c \
	gstasteriskh263.c \
	gstrtpmp4vdepay.c \
	gstrtpmp4vpay.c \
	gstrtpmp4gpay.c \
	gstrtpspeexdepay.c \
	gstrtpspeexpay.c
	
#gstrtpL16pay.c gstrtpL16depay.c

if HAVE_WINSOCK2_H
WINSOCK2_LIBS = -lws2_32
else
WINSOCK2_LIBS =
endif

libgstrtp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_CFLAGS)
libgstrtp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) $(GST_BASE_LIBS) \
                -lgstrtp-@GST_MAJORMINOR@ $(WINSOCK2_LIBS)
libgstrtp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 

noinst_HEADERS =			\
		 gstrtpL16depay.h 	\
		 gstrtpL16pay.h 	\
		 gstrtpamrdepay.h 	\
		 gstrtpamrpay.h 	\
		 gstrtppcmadepay.h	\
		 gstrtppcmudepay.h	\
		 gstrtppcmupay.h 	\
		 gstrtppcmapay.h 	\
		 gstrtpgsmdepay.h 	\
		 gstrtpgsmpay.h 	\
		 gstrtpmpadepay.h 	\
		 gstrtpmpapay.h 	\
		 gstrtph263pdepay.h 	\
		 gstrtph263ppay.h 	\
		 gstrtph263pay.h 	\
		 gstrtpilbcpay.h \
		 gstrtpilbcdepay.h \
		 gstrtpmp4vdepay.h	\
		 gstrtpmp4vpay.h	\
		 gstrtpmp4gpay.h	\
		 gstrtpdepay.h		\
		 gstasteriskh263.h	\
		 gstrtpspeexdepay.h	\
		 gstrtpspeexpay.h
