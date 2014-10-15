plugin_LTLIBRARIES = libgstrtp.la

libgstrtp_la_SOURCES = \
	dboolhuff.c \
	fnv1hash.c \
	gstrtp.c \
	gstrtpchannels.c \
	gstrtpac3depay.c \
	gstrtpac3pay.c \
	gstrtpbvdepay.c \
	gstrtpbvpay.c \
	gstrtpceltdepay.c \
	gstrtpceltpay.c \
	gstrtpdvdepay.c \
	gstrtpdvpay.c \
	gstrtpgstdepay.c \
	gstrtpgstpay.c \
	gstrtpilbcdepay.c \
	gstrtpilbcpay.c \
	gstrtpmpadepay.c \
	gstrtpmpapay.c \
	gstrtpmparobustdepay.c \
	gstrtpmpvdepay.c \
	gstrtpmpvpay.c \
	gstrtppcmadepay.c \
	gstrtppcmudepay.c \
	gstrtppcmupay.c \
	gstrtppcmapay.c \
	gstrtpg722depay.c \
	gstrtpg722pay.c \
	gstrtpg723depay.c \
	gstrtpg723pay.c \
	gstrtpg726pay.c \
	gstrtpg726depay.c \
	gstrtpg729pay.c \
	gstrtpg729depay.c \
	gstrtpgsmdepay.c \
	gstrtpgsmpay.c \
	gstrtpamrdepay.c \
	gstrtpamrpay.c \
	gstrtph263pdepay.c \
	gstrtph263ppay.c \
	gstrtph263depay.c \
	gstrtph263pay.c \
	gstrtph264depay.c \
	gstrtph264pay.c \
	gstrtpj2kdepay.c \
	gstrtpj2kpay.c \
	gstrtpjpegdepay.c \
	gstrtpjpegpay.c \
	gstrtpL16depay.c \
	gstrtpL16pay.c \
	gstrtpL24depay.c \
	gstrtpL24pay.c \
	gstasteriskh263.c \
	gstrtpmp1sdepay.c \
	gstrtpmp2tdepay.c \
	gstrtpmp2tpay.c \
	gstrtpmp4vdepay.c \
	gstrtpmp4vpay.c \
	gstrtpmp4gdepay.c \
	gstrtpmp4gpay.c \
	gstrtpmp4adepay.c \
	gstrtpmp4apay.c \
	gstrtpqcelpdepay.c \
	gstrtpqdmdepay.c \
	gstrtpsbcdepay.c \
	gstrtpsbcpay.c \
	gstrtpsirenpay.c \
	gstrtpsirendepay.c \
	gstrtpspeexdepay.c \
	gstrtpspeexpay.c \
	gstrtpsv3vdepay.c \
	gstrtptheoradepay.c \
	gstrtptheorapay.c \
	gstrtpvorbisdepay.c \
	gstrtpvorbispay.c  \
	gstrtpvp8depay.c \
	gstrtpvp8pay.c \
	gstrtpvrawdepay.c  \
	gstrtpvrawpay.c \
	gstrtpstreampay.c \
	gstrtpstreamdepay.c

libgstrtp_la_CFLAGS = $(GST_PLUGINS_BASE_CFLAGS) $(GST_BASE_CFLAGS) \
	$(GST_CFLAGS) -Dvp8_norm=gst_rtpvp8_vp8_norm \
	-Dvp8dx_start_decode=gst_rtpvp8_vp8dx_start_decode \
	-Dvp8dx_bool_decoder_fill=gst_rtpvp8_vp8dx_bool_decoder_fill

libgstrtp_la_LIBADD = $(GST_PLUGINS_BASE_LIBS) \
	-lgstaudio-@GST_API_VERSION@ \
	-lgstvideo-@GST_API_VERSION@ \
	-lgsttag-@GST_API_VERSION@ \
	-lgstrtp-@GST_API_VERSION@ \
	-lgstpbutils-@GST_API_VERSION@ \
	$(GST_BASE_LIBS) $(GST_LIBS) \
	$(LIBM)
libgstrtp_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS) 
libgstrtp_la_LIBTOOLFLAGS = $(GST_PLUGIN_LIBTOOLFLAGS)

noinst_HEADERS =			\
	dboolhuff.h \
	fnv1hash.h \
	gstrtpchannels.h \
	gstrtpL16depay.h \
	gstrtpL16pay.h \
	gstrtpL24depay.h \
	gstrtpL24pay.h \
	gstrtpac3depay.h \
	gstrtpac3pay.h \
	gstrtpbvdepay.h \
	gstrtpbvpay.h \
	gstrtpceltpay.h	\
	gstrtpceltdepay.h \
	gstrtpdvdepay.h \
	gstrtpdvpay.h \
	gstrtpamrdepay.h \
	gstrtpamrpay.h 	\
	gstrtpgstdepay.h \
	gstrtpgstpay.h 	\
	gstrtpilbcdepay.h \
	gstrtpilbcpay.h \
	gstrtppcmadepay.h \
	gstrtppcmudepay.h \
	gstrtppcmupay.h \
	gstrtppcmapay.h \
	gstrtpg722depay.h \
	gstrtpg722pay.h \
	gstrtpg723depay.h\
	gstrtpg723pay.h \
	gstrtpg726depay.h \
	gstrtpg726pay.h \
	gstrtpg729depay.h \
	gstrtpg729pay.h \
	gstrtpgsmdepay.h \
	gstrtpgsmpay.h 	\
	gstrtpmpadepay.h \
	gstrtpmparobustdepay.h \
	gstrtpmpapay.h \
	gstrtpmpvdepay.h \
	gstrtpmpvpay.h \
	gstrtph263pdepay.h \
	gstrtph263ppay.h \
	gstrtph263depay.h \
	gstrtph263pay.h \
	gstrtph264depay.h \
	gstrtph264pay.h \
	gstrtpj2kdepay.h \
	gstrtpj2kpay.h \
	gstrtpjpegdepay.h \
	gstrtpjpegpay.h \
	gstrtpmp1sdepay.h \
	gstrtpmp2tdepay.h \
	gstrtpmp2tpay.h \
	gstrtpmp4vdepay.h \
	gstrtpmp4vpay.h	\
	gstrtpmp4gdepay.h \
	gstrtpmp4gpay.h	\
	gstrtpmp4adepay.h \
	gstrtpmp4apay.h \
	gstasteriskh263.h \
	gstrtpqcelpdepay.h \
	gstrtpqdmdepay.h \
	gstrtpsbcdepay.h \
	gstrtpsbcpay.h \
	gstrtpsirenpay.h \
	gstrtpsirendepay.h \
	gstrtpspeexdepay.h \
	gstrtpspeexpay.h \
	gstrtpsv3vdepay.h \
	gstrtptheoradepay.h \
	gstrtptheorapay.h \
	gstrtpvorbisdepay.h \
	gstrtpvorbispay.h \
	gstrtpvp8depay.h \
	gstrtpvp8pay.h \
	gstrtpvrawdepay.h \
	gstrtpvrawpay.h \
	gstrtpstreampay.h \
	gstrtpstreamdepay.h

EXTRA_DIST = dboolhuff.LICENSE

Android.mk: Makefile.am $(BUILT_SOURCES)
	androgenizer \
	-:PROJECT libgstrtp -:SHARED libgstrtp \
	 -:TAGS eng debug \
         -:REL_TOP $(top_srcdir) -:ABS_TOP $(abs_top_srcdir) \
	 -:SOURCES $(libgstrtp_la_SOURCES) \
	 -:CFLAGS $(DEFS) $(DEFAULT_INCLUDES) $(libgstrtp_la_CFLAGS) \
	 -:LDFLAGS $(libgstrtp_la_LDFLAGS) \
	  $(libgstrtp_la_LIBADD) \
	  -ldl \
	 -:PASSTHROUGH LOCAL_ARM_MODE:=arm \
	      LOCAL_MODULE_PATH:='$$(TARGET_OUT)/lib/gstreamer-0.10' \
	> $@
