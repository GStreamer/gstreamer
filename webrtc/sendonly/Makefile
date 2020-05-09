CC	:= gcc
LIBS	:= $(shell pkg-config --libs --cflags gstreamer-webrtc-1.0 gstreamer-sdp-1.0 libsoup-2.4 json-glib-1.0)
CFLAGS	:= -O0 -ggdb -Wall -fno-omit-frame-pointer

all: webrtc-unidirectional-h264 webrtc-recvonly-h264

webrtc-unidirectional-h264: webrtc-unidirectional-h264.c
	"$(CC)" $(CFLAGS) $^ $(LIBS) -o $@

webrtc-recvonly-h264: webrtc-recvonly-h264.c
	"$(CC)" $(CFLAGS) $^ $(LIBS) -o $@
