noinst_PROGRAMS = server-alsasrc-PCMA client-PCMA \
	client-rtpaux server-rtpaux client-rtpbundle server-rtpbundle

# FIXME 0.11: ignore GValueArray warnings for now until this is sorted
ERROR_CFLAGS=

server_rtpaux_SOURCES = server-rtpaux.c
server_rtpaux_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
server_rtpaux_LDADD = $(GST_LIBS)

client_rtpaux_SOURCES = client-rtpaux.c
client_rtpaux_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
client_rtpaux_LDADD = $(GST_LIBS)

server_rtpbundle_SOURCES = server-rtpbundle.c
server_rtpbundle_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
server_rtpbundle_LDADD = $(GST_LIBS)

client_rtpbundle_SOURCES = client-rtpbundle.c
client_rtpbundle_CFLAGS = $(GST_CFLAGS) $(GST_PLUGINS_BASE_CFLAGS)
client_rtpbundle_LDADD = $(GST_LIBS)

server_alsasrc_PCMA_SOURCES = server-alsasrc-PCMA.c
server_alsasrc_PCMA_CFLAGS = $(GST_CFLAGS)
server_alsasrc_PCMA_LDADD = $(GST_LIBS) $(LIBM)

client_PCMA_SOURCES = client-PCMA.c
client_PCMA_CFLAGS = $(GST_CFLAGS)
client_PCMA_LDADD = $(GST_LIBS) $(LIBM)

noinst_SCRIPTS=client-H263p-AMR.sh \
               client-H263p-PCMA.sh \
               client-H264-PCMA.sh \
               client-VP8-OPUS.sh \
               client-PCMA.sh \
               server-alsasrc-PCMA.sh \
               server-v4l2-H263p-alsasrc-AMR.sh \
               server-v4l2-H264-alsasrc-PCMA.sh \
               server-VTS-H263p-ATS-PCMA.sh \
               server-VTS-VP8-ATS-OPUS.sh

EXTRA_DIST= $(noinst_SCRIPTS) \
	    client-H263p-PCMA.sdp \
            client-H264-PCMA.sdp
