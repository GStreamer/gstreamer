CC     := gcc
LIBS   := $(shell pkg-config --libs --cflags glib-2.0 gstreamer-1.0 gstreamer-rtp-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0 json-glib-1.0 libsoup-2.4 gstreamer-webrtc-nice-1.0)
CFLAGS := -O0 -ggdb -Wall -fno-omit-frame-pointer \
		$(shell pkg-config --cflags glib-2.0 gstreamer-1.0 gstreamer-rtp-1.0 gstreamer-sdp-1.0 gstreamer-webrtc-1.0 json-glib-1.0 libsoup-2.4)
webrtc-sendrecv: webrtc-sendrecv.c custom_agent.c custom_agent.h
		"$(CC)" $(CFLAGS) $^ $(LIBS) -o $@
