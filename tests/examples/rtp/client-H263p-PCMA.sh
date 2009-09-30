#!/bin/sh
#
# A simple RTP receiver 
#

VIDEO_CAPS="application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H263-1998"
AUDIO_CAPS="application/x-rtp,media=(string)audio,clock-rate=(int)8000,encoding-name=(string)PCMA"

#DEST=192.168.1.126
DEST=localhost

LATENCY=100

gst-launch -v gstrtpbin name=rtpbin latency=$LATENCY                                    \
           udpsrc caps=$VIDEO_CAPS port=5000 ! rtpbin.recv_rtp_sink_0                   \
	         rtpbin. ! rtph263pdepay ! ffdec_h263 ! xvimagesink                     \
           udpsrc port=5001 ! rtpbin.recv_rtcp_sink_0                                   \
           rtpbin.send_rtcp_src_0 ! udpsink host=$DEST port=5005 sync=false async=false \
	   udpsrc caps=$AUDIO_CAPS port=5002 ! rtpbin.recv_rtp_sink_1                   \
	         rtpbin. ! rtppcmadepay ! alawdec ! autoaudiosink buffer-time=10000          \
           udpsrc port=5003 ! rtpbin.recv_rtcp_sink_1                                   \
           rtpbin.send_rtcp_src_1 ! udpsink host=$DEST port=5007 sync=false async=false
