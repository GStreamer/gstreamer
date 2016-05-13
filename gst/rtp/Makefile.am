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
	gstrtph261pay.c \
	gstrtph261depay.c \
	gstrtph263pdepay.c \
	gstrtph263ppay.c \
	gstrtph263depay.c \
	gstrtph263pay.c \
	gstrtph264depay.c \
	gstrtph264pay.c \
	gstrtph265depay.c \
	gstrtph265pay.c \
	gstrtpj2kdepay.c \
	gstrtpj2kpay.c \
	gstrtpjpegdepay.c \
	gstrtpjpegpay.c \
	gstrtpklvdepay.c \
	gstrtpklvpay.c \
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
	gstrtpopusdepay.c \
	gstrtpopuspay.c \
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
	gstrtpvp9depay.c \
	gstrtpvp9pay.c \
	gstrtpvrawdepay.c  \
	gstrtpvrawpay.c \
	gstrtpstreampay.c \
	gstrtpstreamdepay.c \
	gstrtputils.c

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
	gstrtph261pay.h \
	gstrtph261depay.h \
	gstrtph263pdepay.h \
	gstrtph263ppay.h \
	gstrtph263depay.h \
	gstrtph263pay.h \
	gstrtph264depay.h \
	gstrtph264pay.h \
	gstrtph265depay.h \
	gstrtph265pay.h \
	gstrtph265types.h \
	gstrtpj2kdepay.h \
	gstrtpj2kpay.h \
	gstrtpj2kcommon.h \
	gstrtpjpegdepay.h \
	gstrtpjpegpay.h \
	gstrtpklvdepay.h \
	gstrtpklvpay.h \
	gstrtpmp1sdepay.h \
	gstrtpmp2tdepay.h \
	gstrtpmp2tpay.h \
	gstrtpmp4vdepay.h \
	gstrtpmp4vpay.h	\
	gstrtpmp4gdepay.h \
	gstrtpmp4gpay.h	\
	gstrtpmp4adepay.h \
	gstrtpmp4apay.h \
	gstrtpopusdepay.h \
	gstrtpopuspay.h \
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
	gstrtpvp9depay.h \
	gstrtpvp9pay.h \
	gstrtpvrawdepay.h \
	gstrtpvrawpay.h \
	gstrtpstreampay.h \
	gstrtpstreamdepay.h \
	gstrtputils.h

EXTRA_DIST = dboolhuff.LICENSE
