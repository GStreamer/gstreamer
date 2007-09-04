#!/bin/sh
#
# A simple RTP server 
#

gst-launch -v gstrtpbin name=rtpbin \
           alsasrc ! alawenc ! rtppcmapay ! rtpbin.send_rtp_sink_0  \
	             rtpbin.send_rtp_src_0 ! udpsink port=5000                                 \
	             rtpbin.send_rtcp_src_0 ! udpsink port=5001 sync=false async=false         \
                     udpsrc port=5003 ! rtpbin.recv_rtcp_sink_0
