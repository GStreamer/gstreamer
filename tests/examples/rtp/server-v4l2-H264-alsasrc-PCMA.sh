#!/bin/sh
#
# A simple RTP server 
#

AOFFSET=0
VOFFSET=0

# H264 encode from a v4l2src
VCAPS="video/x-raw-yuv,width=352,height=288,framerate=15/1"
VSOURCE="v4l2src ! $VCAPS ! videorate ! ffmpegcolorspace"
VENC="x264enc byte-stream=true bitrate=300 ! rtph264pay"

# PCMA encode from an alsasrc
ASOURCE="alsasrc ! audioconvert"
AENC="alawenc ! rtppcmapay"

gst-launch -v gstrtpbin name=rtpbin \
           $VSOURCE ! $VENC ! rtpbin.send_rtp_sink_0                                           \
                     rtpbin.send_rtp_src_0 ! queue ! udpsink port=5000 ts-offset=$AOFFSET      \
                     rtpbin.send_rtcp_src_0 ! udpsink port=5001 sync=false async=false         \
                     udpsrc port=5005 ! rtpbin.recv_rtcp_sink_0                                \
           $ASOURCE ! $AENC ! rtpbin.send_rtp_sink_1                                           \
	             rtpbin.send_rtp_src_1 ! queue ! udpsink port=5002 ts-offset=$VOFFSET      \
	             rtpbin.send_rtcp_src_1 ! udpsink port=5003 sync=false async=false         \
                     udpsrc port=5007 ! rtpbin.recv_rtcp_sink_1
