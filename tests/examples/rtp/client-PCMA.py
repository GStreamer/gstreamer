#! /usr/bin/env python 

import pygst
pygst.require("0.10")
import gst
import gobject

#
# A simple RTP receiver 
#
#  receives alaw encoded RTP audio on port 5002, RTCP is received on  port 5003.
#  the receiver RTCP reports are sent to port 5007
#
#             .-------.      .----------.     .---------.   .-------.   .--------.
#  RTP        |udpsrc |      | rtpbin   |     |pcmadepay|   |alawdec|   |alsasink|
#  port=5002  |      src->recv_rtp recv_rtp->sink     src->sink   src->sink      |
#             '-------'      |          |     '---------'   '-------'   '--------'
#                            |          |      
#                            |          |     .-------.
#                            |          |     |udpsink|  RTCP
#                            |    send_rtcp->sink     | port=5007
#             .-------.      |          |     '-------' sync=false
#  RTCP       |udpsrc |      |          |               async=false
#  port=5003  |     src->recv_rtcp      |                       
#             '-------'      '----------'              

AUDIO_CAPS = 'application/x-rtp,media=(string)audio,clock-rate=(int)8000,encoding-name=(string)PCMA'
AUDIO_DEPAY = 'rtppcmadepay'
AUDIO_DEC = 'alawdec'
AUDIO_SINK = 'autoaudiosink'

DEST = '127.0.0.1'

RTP_RECV_PORT = 5002
RTCP_RECV_PORT = 5003
RTCP_SEND_PORT = 5007 

#gst-launch -v rtpbin name=rtpbin                                                \
#       udpsrc caps=$AUDIO_CAPS port=$RTP_RECV_PORT ! rtpbin.recv_rtp_sink_0              \
#             rtpbin. ! rtppcmadepay ! alawdec ! audioconvert ! audioresample ! autoaudiosink \
#           udpsrc port=$RTCP_RECV_PORT ! rtpbin.recv_rtcp_sink_0                              \
#         rtpbin.send_rtcp_src_0 ! udpsink port=$RTCP_SEND_PORT host=$DEST sync=false async=false

def pad_added_cb(rtpbin, new_pad, depay):
    sinkpad = gst.Element.get_static_pad(depay, 'sink')
    lres = gst.Pad.link(new_pad, sinkpad)

# the pipeline to hold eveything 
pipeline = gst.Pipeline('rtp_client')

# the udp src and source we will use for RTP and RTCP
rtpsrc = gst.element_factory_make('udpsrc', 'rtpsrc')
rtpsrc.set_property('port', RTP_RECV_PORT)

# we need to set caps on the udpsrc for the RTP data
caps = gst.caps_from_string(AUDIO_CAPS)
rtpsrc.set_property('caps', caps)

rtcpsrc = gst.element_factory_make('udpsrc', 'rtcpsrc')
rtcpsrc.set_property('port', RTCP_RECV_PORT)

rtcpsink = gst.element_factory_make('udpsink', 'rtcpsink')
rtcpsink.set_property('port', RTCP_SEND_PORT)
rtcpsink.set_property('host', DEST)

# no need for synchronisation or preroll on the RTCP sink
rtcpsink.set_property('async', False)
rtcpsink.set_property('sync', False) 

pipeline.add(rtpsrc, rtcpsrc, rtcpsink)

# the depayloading and decoding
audiodepay = gst.element_factory_make(AUDIO_DEPAY, 'audiodepay')
audiodec = gst.element_factory_make(AUDIO_DEC, 'audiodec')

# the audio playback and format conversion
audioconv = gst.element_factory_make('audioconvert', 'audioconv')
audiores = gst.element_factory_make('audioresample', 'audiores')
audiosink = gst.element_factory_make(AUDIO_SINK, 'audiosink')

# add depayloading and playback to the pipeline and link
pipeline.add(audiodepay, audiodec, audioconv, audiores, audiosink)

res = gst.element_link_many(audiodepay, audiodec, audioconv, audiores, audiosink)

# the rtpbin element
rtpbin = gst.element_factory_make('rtpbin', 'rtpbin')

pipeline.add(rtpbin)

# now link all to the rtpbin, start by getting an RTP sinkpad for session 0
srcpad = gst.Element.get_static_pad(rtpsrc, 'src')
sinkpad = gst.Element.get_request_pad(rtpbin, 'recv_rtp_sink_0')
lres = gst.Pad.link(srcpad, sinkpad)

# get an RTCP sinkpad in session 0
srcpad = gst.Element.get_static_pad(rtcpsrc, 'src')
sinkpad = gst.Element.get_request_pad(rtpbin, 'recv_rtcp_sink_0')
lres = gst.Pad.link(srcpad, sinkpad)

# get an RTCP srcpad for sending RTCP back to the sender
srcpad = gst.Element.get_request_pad(rtpbin, 'send_rtcp_src_0')
sinkpad = gst.Element.get_static_pad(rtcpsink, 'sink')
lres = gst.Pad.link(srcpad, sinkpad)

rtpbin.connect('pad-added', pad_added_cb, audiodepay) 

gst.Element.set_state(pipeline, gst.STATE_PLAYING)

mainloop = gobject.MainLoop()
mainloop.run() 

gst.Element.set_state(pipeline, gst.STATE_NULL) 


