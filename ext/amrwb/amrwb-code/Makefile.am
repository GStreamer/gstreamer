noinst_LTLIBRARIES = libamrwb.la 

nodist_libamrwb_la_SOURCES = 	\
	enc_acelp.c \
	enc_dtx.c \
	enc_gain.c \
	enc_if.c \
	enc_lpc.c \
	enc_main.c \
	enc_rom.c \
	enc_util.c \
	if_rom.c \
	dec_acelp.c \
	dec_dtx.c \
	dec_gain.c \
	dec_if.c \
	dec_lpc.c \
	dec_main.c \
	dec_rom.c \
	dec_util.c 

libamrwb_la_CFLAGS = -I./amrwb-code/c-code
libamrwb_la_LIBADD = $(GST_BASE_LIBS) $(AMRWB_LIBS) 
libamrwb_la_LDFLAGS = $(GST_PLUGIN_LDFLAGS)

nodist_noinst_HEADERS =	\
	dec_acelp.h \
	dec_dtx.h \
	dec_gain.h \
	dec.h \
	dec_if.h \
	dec_lpc.h \
	dec_main.h \
	dec_util.h \
	enc_acelp.h \
	enc_dtx.h \
	enc_gain.h \
	enc.h \
	enc_if.h \
	enc_lpc.h \
	enc_main.h \
	enc_util.h \
	if_rom.h \
	typedef.h
