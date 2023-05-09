#!/usr/bin/env python3
#
# Copyright (C) 2018 Matthew Waters <matthew@centricular.com>
#               2022 Nirbheek Chauhan <nirbheek@centricular.com>
#
# Demo gstreamer app for negotiating and streaming a sendrecv webrtc stream
# with a browser JS app, implemented in Python.

from websockets.version import version as wsv
import random
import ssl
import websockets
import asyncio
import os
import sys
import json
import argparse

import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst  # NOQA
gi.require_version('GstWebRTC', '1.0')
from gi.repository import GstWebRTC  # NOQA
gi.require_version('GstSdp', '1.0')
from gi.repository import GstSdp  # NOQA

# Ensure that gst-python is installed
try:
    from gi.overrides import Gst as _
except ImportError:
    print('gstreamer-python binding overrides aren\'t available, please install them')
    raise

# These properties all mirror the ones in webrtc-sendrecv.c, see there for explanations
PIPELINE_DESC_VP8 = '''
webrtcbin name=sendrecv latency=0 stun-server=stun://stun.l.google.com:19302
 videotestsrc is-live=true pattern=ball ! videoconvert ! queue !
  vp8enc deadline=1 keyframe-max-dist=2000 ! rtpvp8pay picture-id-mode=15-bit !
  queue ! application/x-rtp,media=video,encoding-name=VP8,payload={video_pt} ! sendrecv.
 audiotestsrc is-live=true ! audioconvert ! audioresample ! queue ! opusenc ! rtpopuspay !
  queue ! application/x-rtp,media=audio,encoding-name=OPUS,payload={audio_pt} ! sendrecv.
'''
PIPELINE_DESC_H264 = '''
webrtcbin name=sendrecv latency=0 stun-server=stun://stun.l.google.com:19302
 videotestsrc is-live=true pattern=ball ! videoconvert ! queue !
  x264enc tune=zerolatency speed-preset=ultrafast key-int-max=30 intra-refresh=true !
  rtph264pay aggregate-mode=zero-latency config-interval=-1 !
  queue ! application/x-rtp,media=video,encoding-name=H264,payload={video_pt} ! sendrecv.
 audiotestsrc is-live=true ! audioconvert ! audioresample ! queue ! opusenc ! rtpopuspay !
  queue ! application/x-rtp,media=audio,encoding-name=OPUS,payload={audio_pt} ! sendrecv.
'''
# Force I420 because dav1d bundled with Chrome doesn't support 10-bit choma/luma (I420_10LE)
PIPELINE_DESC_AV1 = '''
webrtcbin name=sendrecv latency=0 stun-server=stun://stun.l.google.com:19302
 videotestsrc is-live=true pattern=ball ! videoconvert ! queue !
  video/x-raw,format=I420 ! svtav1enc preset=13 ! av1parse ! rtpav1pay !
  queue ! application/x-rtp,media=video,encoding-name=AV1,payload={video_pt} ! sendrecv.
 audiotestsrc is-live=true ! audioconvert ! audioresample ! queue ! opusenc ! rtpopuspay !
  queue ! application/x-rtp,media=audio,encoding-name=OPUS,payload={audio_pt} ! sendrecv.
'''

PIPELINE_DESC = {
    'AV1': PIPELINE_DESC_AV1,
    'H264': PIPELINE_DESC_H264,
    'VP8': PIPELINE_DESC_VP8,
}


def print_status(msg):
    print(f'--- {msg}')


def print_error(msg):
    print(f'!!! {msg}', file=sys.stderr)


def get_payload_types(sdpmsg, video_encoding, audio_encoding):
    '''
    Find the payload types for the specified video and audio encoding.

    Very simplistically finds the first payload type matching the encoding
    name. More complex applications will want to match caps on
    profile-level-id, packetization-mode, etc.
    '''
    video_pt = None
    audio_pt = None
    for i in range(0, sdpmsg.medias_len()):
        media = sdpmsg.get_media(i)
        for j in range(0, media.formats_len()):
            fmt = media.get_format(j)
            if fmt == 'webrtc-datachannel':
                continue
            pt = int(fmt)
            caps = media.get_caps_from_media(pt)
            s = caps.get_structure(0)
            encoding_name = s['encoding-name']
            if video_pt is None and encoding_name == video_encoding:
                video_pt = pt
            elif audio_pt is None and encoding_name == audio_encoding:
                audio_pt = pt
    return {video_encoding: video_pt, audio_encoding: audio_pt}


class WebRTCClient:
    def __init__(self, loop, our_id, peer_id, server, remote_is_offerer, video_encoding):
        self.conn = None
        self.pipe = None
        self.webrtc = None
        self.event_loop = loop
        self.server = server
        # An optional user-specified ID we can use to register
        self.our_id = our_id
        # The actual ID we used to register
        self.id_ = None
        # An optional peer ID we should connect to
        self.peer_id = peer_id
        # Whether we will send the offer or the remote peer will
        self.remote_is_offerer = remote_is_offerer
        # Video encoding: VP8, H264, etc
        self.video_encoding = video_encoding.upper()

    async def send(self, msg):
        assert self.conn
        print(f'>>> {msg}')
        await self.conn.send(msg)

    async def connect(self):
        self.conn = await websockets.connect(self.server)
        if self.our_id is None:
            self.id_ = str(random.randrange(10, 10000))
        else:
            self.id_ = self.our_id
        await self.send(f'HELLO {self.id_}')

    async def setup_call(self):
        assert self.peer_id
        await self.send(f'SESSION {self.peer_id}')

    def send_soon(self, msg):
        asyncio.run_coroutine_threadsafe(self.send(msg), self.event_loop)

    def on_bus_poll_cb(self, bus):
        def remove_bus_poll():
            self.event_loop.remove_reader(bus.get_pollfd().fd)
            self.event_loop.stop()
        while bus.peek():
            msg = bus.pop()
            if msg.type == Gst.MessageType.ERROR:
                err = msg.parse_error()
                print("ERROR:", err.gerror, err.debug)
                remove_bus_poll()
                break
            elif msg.type == Gst.MessageType.EOS:
                remove_bus_poll()
                break
            elif msg.type == Gst.MessageType.LATENCY:
                self.pipe.recalculate_latency()

    def send_sdp(self, offer):
        text = offer.sdp.as_text()
        if offer.type == GstWebRTC.WebRTCSDPType.OFFER:
            print_status('Sending offer:\n%s' % text)
            msg = json.dumps({'sdp': {'type': 'offer', 'sdp': text}})
        elif offer.type == GstWebRTC.WebRTCSDPType.ANSWER:
            print_status('Sending answer:\n%s' % text)
            msg = json.dumps({'sdp': {'type': 'answer', 'sdp': text}})
        else:
            raise AssertionError(offer.type)
        self.send_soon(msg)

    def on_offer_created(self, promise, _, __):
        assert promise.wait() == Gst.PromiseResult.REPLIED
        reply = promise.get_reply()
        offer = reply['offer']
        promise = Gst.Promise.new()
        print_status('Offer created, setting local description')
        self.webrtc.emit('set-local-description', offer, promise)
        promise.interrupt()  # we don't care about the result, discard it
        self.send_sdp(offer)

    def on_negotiation_needed(self, _, create_offer):
        if create_offer:
            print_status('Call was connected: creating offer')
            promise = Gst.Promise.new_with_change_func(self.on_offer_created, None, None)
            self.webrtc.emit('create-offer', None, promise)

    def send_ice_candidate_message(self, _, mlineindex, candidate):
        icemsg = json.dumps({'ice': {'candidate': candidate, 'sdpMLineIndex': mlineindex}})
        self.send_soon(icemsg)

    def on_incoming_decodebin_stream(self, _, pad):
        if not pad.has_current_caps():
            print_error(pad, 'has no caps, ignoring')
            return

        caps = pad.get_current_caps()
        assert (len(caps))
        s = caps[0]
        name = s.get_name()
        if name.startswith('video'):
            q = Gst.ElementFactory.make('queue')
            conv = Gst.ElementFactory.make('videoconvert')
            sink = Gst.ElementFactory.make('autovideosink')
            self.pipe.add(q, conv, sink)
            self.pipe.sync_children_states()
            pad.link(q.get_static_pad('sink'))
            q.link(conv)
            conv.link(sink)
        elif name.startswith('audio'):
            q = Gst.ElementFactory.make('queue')
            conv = Gst.ElementFactory.make('audioconvert')
            resample = Gst.ElementFactory.make('audioresample')
            sink = Gst.ElementFactory.make('autoaudiosink')
            self.pipe.add(q, conv, resample, sink)
            self.pipe.sync_children_states()
            pad.link(q.get_static_pad('sink'))
            q.link(conv)
            conv.link(resample)
            resample.link(sink)

    def on_ice_gathering_state_notify(self, pspec, _):
        state = self.webrtc.get_property('ice-gathering-state')
        print_status(f'ICE gathering state changed to {state}')

    def on_incoming_stream(self, _, pad):
        if pad.direction != Gst.PadDirection.SRC:
            return

        decodebin = Gst.ElementFactory.make('decodebin')
        decodebin.connect('pad-added', self.on_incoming_decodebin_stream)
        self.pipe.add(decodebin)
        decodebin.sync_state_with_parent()
        pad.link(decodebin.get_static_pad('sink'))

    def start_pipeline(self, create_offer=True, audio_pt=96, video_pt=97):
        print_status(f'Creating pipeline, create_offer: {create_offer}')
        self.pipe = Gst.parse_launch(PIPELINE_DESC[self.video_encoding].format(video_pt=video_pt, audio_pt=audio_pt))
        bus = self.pipe.get_bus()
        self.event_loop.add_reader(bus.get_pollfd().fd, self.on_bus_poll_cb, bus)
        self.webrtc = self.pipe.get_by_name('sendrecv')
        self.webrtc.connect('on-negotiation-needed', self.on_negotiation_needed, create_offer)
        self.webrtc.connect('on-ice-candidate', self.send_ice_candidate_message)
        self.webrtc.connect('notify::ice-gathering-state', self.on_ice_gathering_state_notify)
        self.webrtc.connect('pad-added', self.on_incoming_stream)
        self.pipe.set_state(Gst.State.PLAYING)

    def on_answer_created(self, promise, _, __):
        assert promise.wait() == Gst.PromiseResult.REPLIED
        reply = promise.get_reply()
        answer = reply['answer']
        promise = Gst.Promise.new()
        self.webrtc.emit('set-local-description', answer, promise)
        promise.interrupt()  # we don't care about the result, discard it
        self.send_sdp(answer)

    def on_offer_set(self, promise, _, __):
        assert promise.wait() == Gst.PromiseResult.REPLIED
        promise = Gst.Promise.new_with_change_func(self.on_answer_created, None, None)
        self.webrtc.emit('create-answer', None, promise)

    def handle_json(self, message):
        try:
            msg = json.loads(message)
        except json.decoder.JSONDecoderError:
            print_error('Failed to parse JSON message, this might be a bug')
            raise
        if 'sdp' in msg:
            sdp = msg['sdp']['sdp']
            if msg['sdp']['type'] == 'answer':
                print_status('Received answer:\n%s' % sdp)
                res, sdpmsg = GstSdp.SDPMessage.new_from_text(sdp)
                answer = GstWebRTC.WebRTCSessionDescription.new(GstWebRTC.WebRTCSDPType.ANSWER, sdpmsg)
                promise = Gst.Promise.new()
                self.webrtc.emit('set-remote-description', answer, promise)
                promise.interrupt()  # we don't care about the result, discard it
            else:
                print_status('Received offer:\n%s' % sdp)
                res, sdpmsg = GstSdp.SDPMessage.new_from_text(sdp)

                if not self.webrtc:
                    print_status('Incoming call: received an offer, creating pipeline')
                    pts = get_payload_types(sdpmsg, video_encoding=self.video_encoding, audio_encoding='OPUS')
                    assert self.video_encoding in pts
                    assert 'OPUS' in pts
                    self.start_pipeline(create_offer=False, video_pt=pts[self.video_encoding], audio_pt=pts['OPUS'])

                assert self.webrtc

                offer = GstWebRTC.WebRTCSessionDescription.new(GstWebRTC.WebRTCSDPType.OFFER, sdpmsg)
                promise = Gst.Promise.new_with_change_func(self.on_offer_set, None, None)
                self.webrtc.emit('set-remote-description', offer, promise)
        elif 'ice' in msg:
            assert self.webrtc
            ice = msg['ice']
            candidate = ice['candidate']
            sdpmlineindex = ice['sdpMLineIndex']
            self.webrtc.emit('add-ice-candidate', sdpmlineindex, candidate)
        else:
            print_error('Unknown JSON message')

    def close_pipeline(self):
        if self.pipe:
            self.pipe.set_state(Gst.State.NULL)
            self.pipe = None
        self.webrtc = None

    async def loop(self):
        assert self.conn
        async for message in self.conn:
            print(f'<<< {message}')
            if message == 'HELLO':
                assert self.id_
                # If a peer ID is specified, we want to connect to it. If not,
                # we wait for an incoming call.
                if not self.peer_id:
                    print_status(f'Waiting for incoming call: ID is {self.id_}')
                else:
                    if self.remote_is_offerer:
                        print_status('Have peer ID: initiating call (will request remote peer to create offer)')
                    else:
                        print_status('Have peer ID: initiating call (will create offer)')
                    await self.setup_call()
            elif message == 'SESSION_OK':
                if self.remote_is_offerer:
                    # We are initiating the call, but we want the remote peer to create the offer
                    print_status('Call was connected: requesting remote peer for offer')
                    await self.send('OFFER_REQUEST')
                else:
                    self.start_pipeline()
            elif message == 'OFFER_REQUEST':
                print_status('Incoming call: we have been asked to create the offer')
                self.start_pipeline()
            elif message.startswith('ERROR'):
                print_error(message)
                self.close_pipeline()
                return 1
            else:
                self.handle_json(message)
        self.close_pipeline()
        return 0

    async def stop(self):
        if self.conn:
            await self.conn.close()
        self.conn = None


def check_plugins(video_encoding):
    needed = ["opus", "nice", "webrtc", "dtls", "srtp", "rtp",
              "rtpmanager", "videotestsrc", "audiotestsrc"]
    if video_encoding == 'vp8':
        needed.append('vpx')
    elif video_encoding == 'h264':
        needed += ['x264', 'videoparsersbad']
    elif video_encoding == 'av1':
        needed += ['svtav1', 'videoparsersbad']
    missing = list(filter(lambda p: Gst.Registry.get().find_plugin(p) is None, needed))
    if len(missing):
        print_error(f'Missing gstreamer plugins: {missing}')
        return False
    return True


if __name__ == '__main__':
    Gst.init(None)
    parser = argparse.ArgumentParser()
    parser.add_argument('--video-encoding', default='vp8', nargs='?', choices=['vp8', 'h264', 'av1'],
                        help='Video encoding to negotiate')
    parser.add_argument('--peer-id', help='String ID of the peer to connect to')
    parser.add_argument('--our-id', help='String ID that the peer can use to connect to us')
    parser.add_argument('--server', default='wss://webrtc.gstreamer.net:8443',
                        help='Signalling server to connect to, eg "wss://127.0.0.1:8443"')
    parser.add_argument('--remote-offerer', default=False, action='store_true',
                        dest='remote_is_offerer',
                        help='Request that the peer generate the offer and we\'ll answer')
    args = parser.parse_args()
    if not check_plugins(args.video_encoding):
        sys.exit(1)
    if not args.peer_id and not args.our_id:
        print('You must pass either --peer-id or --our-id')
        sys.exit(1)
    loop = asyncio.new_event_loop()
    c = WebRTCClient(loop, args.our_id, args.peer_id, args.server, args.remote_is_offerer, args.video_encoding)
    loop.run_until_complete(c.connect())
    res = loop.run_until_complete(c.loop())
    sys.exit(res)
