#!/bin/sh
#
# A simple RTP receiver 
#

AUDIO_CAPS="application/x-rtp,media=(string)audio,clock-rate=(int)8000,encoding-name=(string)PCMA"

gst-launch -v gstrtpbin name=rtpbin                                                \
	   udpsrc caps=$AUDIO_CAPS port=5000 ! rtpbin.recv_rtp_sink_0              \
	         rtpbin. ! rtppcmadepay ! alawdec ! alsasink                       \
           udpsrc port=5001 ! rtpbin.recv_rtcp_sink_0                              \
           rtpbin.send_rtcp_src_0 ! udpsink port=5003 sync=false async=false
