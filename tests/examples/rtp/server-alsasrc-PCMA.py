#! /usr/bin/env python 

import gobject, pygst
pygst.require("0.10")
import gst 

#gst-launch -v rtpbin name=rtpbin audiotestsrc ! audioconvert ! alawenc ! rtppcmapay ! rtpbin.send_rtp_sink_0 \
#                rtpbin.send_rtp_src_0 ! udpsink port=10000 host=xxx.xxx.xxx.xxx \
#                rtpbin.send_rtcp_src_0 ! udpsink port=10001 host=xxx.xxx.xxx.xxx sync=false async=false \
#                udpsrc port=10002 ! rtpbin.recv_rtcp_sink_0 

DEST_HOST = '127.0.0.1'

AUDIO_SRC = 'audiotestsrc'
AUDIO_ENC = 'alawenc'
AUDIO_PAY = 'rtppcmapay'

RTP_SEND_PORT = 5002
RTCP_SEND_PORT = 5003
RTCP_RECV_PORT = 5007 

# the pipeline to hold everything
pipeline = gst.Pipeline('rtp_server')

# the pipeline to hold everything
audiosrc = gst.element_factory_make(AUDIO_SRC, 'audiosrc')
audioconv = gst.element_factory_make('audioconvert', 'audioconv')
audiores = gst.element_factory_make('audioresample', 'audiores')

# the pipeline to hold everything
audioenc = gst.element_factory_make(AUDIO_ENC, 'audioenc')
audiopay = gst.element_factory_make(AUDIO_PAY, 'audiopay')

# add capture and payloading to the pipeline and link
pipeline.add(audiosrc, audioconv, audiores, audioenc, audiopay)

res = gst.element_link_many(audiosrc, audioconv, audiores, audioenc, audiopay)

# the rtpbin element
rtpbin = gst.element_factory_make('rtpbin', 'rtpbin')

pipeline.add(rtpbin) 

# the udp sinks and source we will use for RTP and RTCP
rtpsink = gst.element_factory_make('udpsink', 'rtpsink')
rtpsink.set_property('port', RTP_SEND_PORT)
rtpsink.set_property('host', DEST_HOST)

rtcpsink = gst.element_factory_make('udpsink', 'rtcpsink')
rtcpsink.set_property('port', RTCP_SEND_PORT)
rtcpsink.set_property('host', DEST_HOST)
# no need for synchronisation or preroll on the RTCP sink
rtcpsink.set_property('async', False)
rtcpsink.set_property('sync', False) 

rtcpsrc = gst.element_factory_make('udpsrc', 'rtcpsrc')
rtcpsrc.set_property('port', RTCP_RECV_PORT)

pipeline.add(rtpsink, rtcpsink, rtcpsrc) 

# now link all to the rtpbin, start by getting an RTP sinkpad for session 0
sinkpad = gst.Element.get_request_pad(rtpbin, 'send_rtp_sink_0')
srcpad = gst.Element.get_static_pad(audiopay, 'src')
lres = gst.Pad.link(srcpad, sinkpad)

# get the RTP srcpad that was created when we requested the sinkpad above and
# link it to the rtpsink sinkpad
srcpad = gst.Element.get_static_pad(rtpbin, 'send_rtp_src_0')
sinkpad = gst.Element.get_static_pad(rtpsink, 'sink')
lres = gst.Pad.link(srcpad, sinkpad)

# get an RTCP srcpad for sending RTCP to the receiver
srcpad = gst.Element.get_request_pad(rtpbin, 'send_rtcp_src_0')
sinkpad = gst.Element.get_static_pad(rtcpsink, 'sink')
lres = gst.Pad.link(srcpad, sinkpad)

# we also want to receive RTCP, request an RTCP sinkpad for session 0 and
# link it to the srcpad of the udpsrc for RTCP
srcpad = gst.Element.get_static_pad(rtcpsrc, 'src')
sinkpad = gst.Element.get_request_pad(rtpbin, 'recv_rtcp_sink_0')
lres = gst.Pad.link(srcpad, sinkpad)

# set the pipeline to playing
gst.Element.set_state(pipeline, gst.STATE_PLAYING)

# we need to run a GLib main loop to get the messages
mainloop = gobject.MainLoop()
mainloop.run() 

gst.Element.set_state(pipeline, gst.STATE_NULL)
