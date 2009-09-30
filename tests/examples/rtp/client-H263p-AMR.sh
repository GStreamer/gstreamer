#!/bin/sh
#
# A simple RTP receiver 
#

VIDEO_CAPS="application/x-rtp,media=(string)video,clock-rate=(int)90000,encoding-name=(string)H263-1998"
AUDIO_CAPS="application/x-rtp,media=(string)audio,clock-rate=(int)8000,encoding-name=(string)AMR,encoding-params=(string)1,octet-align=(string)1"

gst-launch -v gstrtpbin name=rtpbin latency=100                                    \
           udpsrc caps=$VIDEO_CAPS port=5000 ! rtpbin.recv_rtp_sink_0              \
	         rtpbin. ! rtph263pdepay ! ffdec_h263 ! xvimagesink                \
           udpsrc port=5001 ! rtpbin.recv_rtcp_sink_0                              \
           rtpbin.send_rtcp_src_0 ! udpsink port=5005 sync=false async=false       \
	   udpsrc caps=$AUDIO_CAPS port=5002 ! rtpbin.recv_rtp_sink_1              \
	         rtpbin. ! rtpamrdepay ! amrnbdec ! autoaudiosink slave-method=1        \
           udpsrc port=5003 ! rtpbin.recv_rtcp_sink_1                              \
           rtpbin.send_rtcp_src_1 ! udpsink port=5007 sync=false async=false
