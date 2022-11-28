# Janus Videoroom example
# Copyright @tobiasfriden and @saket424 on github
# See https://github.com/centricular/gstwebrtc-demos/issues/66
# Copyright Jan Schmidt <jan@centricular.com> 2020
import random
import ssl
import websockets
import asyncio
import os
import sys
import json
import argparse
import string
from websockets.exceptions import ConnectionClosed

import attr

# Set to False to send H.264
DO_VP8 = True
# Set to False to disable RTX (lost packet retransmission)
DO_RTX = True
# Choose the video source:
VIDEO_SRC="videotestsrc pattern=ball"
# VIDEO_SRC="v4l2src"


@attr.s
class JanusEvent:
    sender = attr.ib(validator=attr.validators.instance_of(int))

@attr.s
class PluginData(JanusEvent):
    plugin = attr.ib(validator=attr.validators.instance_of(str))
    data = attr.ib()
    jsep = attr.ib()

@attr.s
class WebrtcUp(JanusEvent):
    pass

@attr.s
class Media(JanusEvent):
    receiving = attr.ib(validator=attr.validators.instance_of(bool))
    kind = attr.ib(validator=attr.validators.in_(["audio", "video"]))

    @kind.validator
    def validate_kind(self, attribute, kind):
        if kind not in ["video", "audio"]:
            raise ValueError("kind must equal video or audio")

@attr.s
class SlowLink(JanusEvent):
    uplink = attr.ib(validator=attr.validators.instance_of(bool))
    lost = attr.ib(validator=attr.validators.instance_of(int))

@attr.s
class HangUp(JanusEvent):
    reason = attr.ib(validator=attr.validators.instance_of(str))

@attr.s(cmp=False)
class Ack:
    transaction = attr.ib(validator=attr.validators.instance_of(str))

@attr.s
class Jsep:
    sdp = attr.ib()
    type = attr.ib(validator=attr.validators.in_(["offer", "pranswer", "answer", "rollback"]))


import gi
gi.require_version('Gst', '1.0')
from gi.repository import Gst
gi.require_version('GstWebRTC', '1.0')
from gi.repository import GstWebRTC
gi.require_version('GstSdp', '1.0')
from gi.repository import GstSdp

if DO_VP8:
    ( encoder, payloader, rtp_encoding) = ( "vp8enc target-bitrate=100000 overshoot=25 undershoot=100 deadline=33000 keyframe-max-dist=1", "rtpvp8pay picture-id-mode=2", "VP8" )
else:
    ( encoder, payloader, rtp_encoding) = ( "x264enc", "rtph264pay aggregate-mode=zero-latency", "H264" )

PIPELINE_DESC = '''
 webrtcbin name=sendrecv stun-server=stun://stun.l.google.com:19302
 {} ! video/x-raw,width=640,height=480 ! videoconvert ! queue !
 {} ! {} !  queue ! application/x-rtp,media=video,encoding-name={},payload=96 ! sendrecv.
'''.format(VIDEO_SRC, encoder, payloader, rtp_encoding)

def transaction_id():
    return ''.join(random.choice(string.ascii_uppercase + string.digits) for _ in range(8))

@attr.s
class JanusGateway:
    server = attr.ib(validator=attr.validators.instance_of(str))
    #secure = attr.ib(default=True)
    _messages = attr.ib(factory=set)
    conn = None

    async def connect(self):
        sslCon=None
        if self.server.startswith("wss"):
            sslCon=ssl.SSLContext()
        self.conn = await websockets.connect(self.server, subprotocols=['janus-protocol'], ssl=sslCon)
        transaction = transaction_id()
        await self.conn.send(json.dumps({
            "janus": "create",
            "transaction": transaction
            }))
        resp = await self.conn.recv()
        print (resp)
        parsed = json.loads(resp)
        assert parsed["janus"] == "success", "Failed creating session"
        assert parsed["transaction"] == transaction, "Incorrect transaction"
        self.session = parsed["data"]["id"]

    async def close(self):
        if self.conn:
            await self.conn.close()

    async def attach(self, plugin):
        assert hasattr(self, "session"), "Must connect before attaching to plugin"
        transaction = transaction_id()
        await self.conn.send(json.dumps({
            "janus": "attach",
            "session_id": self.session,
            "plugin": plugin,
            "transaction": transaction
        }))
        resp = await self.conn.recv()
        parsed = json.loads(resp)
        assert parsed["janus"] == "success", "Failed attaching to {}".format(plugin)
        assert parsed["transaction"] == transaction, "Incorrect transaction"
        self.handle = parsed["data"]["id"]

    async def sendtrickle(self, candidate):
        assert hasattr(self, "session"), "Must connect before sending messages"
        assert hasattr(self, "handle"), "Must attach before sending messages"

        transaction = transaction_id()
        janus_message = {
            "janus": "trickle",
            "session_id": self.session,
            "handle_id": self.handle,
            "transaction": transaction,
            "candidate": candidate
        }

        await self.conn.send(json.dumps(janus_message))

        #while True:
        #    resp = await self._recv_and_parse()
        #    if isinstance(resp, PluginData):
        #        return resp
        #    else:
        #        self._messages.add(resp)
#
    async def sendmessage(self, body, jsep=None):
        assert hasattr(self, "session"), "Must connect before sending messages"
        assert hasattr(self, "handle"), "Must attach before sending messages"

        transaction = transaction_id()
        janus_message = {
            "janus": "message",
            "session_id": self.session,
            "handle_id": self.handle,
            "transaction": transaction,
            "body": body
        }
        if jsep is not None:
            janus_message["jsep"] = jsep

        await self.conn.send(json.dumps(janus_message))

        #while True:
        #    resp = await self._recv_and_parse()
        #    if isinstance(resp, PluginData):
        #        if jsep is not None:
        #            await client.handle_sdp(resp.jsep)
        #        return resp
        #    else:
        #        self._messages.add(resp)

    async def keepalive(self):
        assert hasattr(self, "session"), "Must connect before sending messages"
        assert hasattr(self, "handle"), "Must attach before sending messages"

        while True:
            try:
                await asyncio.sleep(10)
                transaction = transaction_id()
                await self.conn.send(json.dumps({
                    "janus": "keepalive",
                    "session_id": self.session,
                    "handle_id": self.handle,
                    "transaction": transaction
                }))
            except KeyboardInterrupt:
                return

    async def recv(self):
        if len(self._messages) > 0:
            return self._messages.pop()
        else:
            return await self._recv_and_parse()

    async def _recv_and_parse(self):
        raw = json.loads(await self.conn.recv())
        janus = raw["janus"]

        if janus == "event":
            return PluginData(
                sender=raw["sender"],
                plugin=raw["plugindata"]["plugin"],
                data=raw["plugindata"]["data"],
                jsep=raw["jsep"] if "jsep" in raw else None
            )
        elif janus == "webrtcup":
            return WebrtcUp(
                sender=raw["sender"]
            )
        elif janus == "media":
            return Media(
                sender=raw["sender"],
                receiving=raw["receiving"],
                kind=raw["type"]
            )
        elif janus == "slowlink":
            return SlowLink(
                sender=raw["sender"],
                uplink=raw["uplink"],
                lost=raw["lost"]
            )
        elif janus == "hangup":
            return HangUp(
                sender=raw["sender"],
                reason=raw["reason"]
            )
        elif janus == "ack":
            return Ack(
                transaction=raw["transaction"]
            )
        else:
            return raw

class WebRTCClient:
    def __init__(self, peer_id, server):
        self.conn = None
        self.pipe = None
        self.webrtc = None
        self.peer_id = peer_id
        self.signaling = JanusGateway(server)
        self.request = None
        self.offermsg = None

    def send_sdp_offer(self, offer):
        text = offer.sdp.as_text()
        print ('Sending offer:\n%s' % text)
        # configure media
        media = {'audio': True, 'video': True}
        request = {'request': 'publish'}
        request.update(media)
        self.request = request
        self.offermsg = { 'sdp': text, 'trickle': True, 'type': 'offer' }
        print (self.offermsg)
        loop = asyncio.new_event_loop()
        loop.run_until_complete(self.signaling.sendmessage(self.request, self.offermsg))

    def on_offer_created(self, promise, _, __):
        promise.wait()
        reply = promise.get_reply()
        offer = reply.get_value('offer')
        promise = Gst.Promise.new()
        self.webrtc.emit('set-local-description', offer, promise)
        promise.interrupt()
        self.send_sdp_offer(offer)

    def on_negotiation_needed(self, element):
        promise = Gst.Promise.new_with_change_func(self.on_offer_created, element, None)
        element.emit('create-offer', None, promise)

    def send_ice_candidate_message(self, _, mlineindex, candidate):
        icemsg = {'candidate': candidate, 'sdpMLineIndex': mlineindex}
        print ("Sending ICE", icemsg)
        loop = asyncio.new_event_loop()
        loop.run_until_complete(self.signaling.sendtrickle(icemsg))

    def on_incoming_decodebin_stream(self, _, pad):
        if not pad.has_current_caps():
            print (pad, 'has no caps, ignoring')
            return

        caps = pad.get_current_caps()
        name = caps.to_string()
        if name.startswith('video'):
            q = Gst.ElementFactory.make('queue')
            conv = Gst.ElementFactory.make('videoconvert')
            sink = Gst.ElementFactory.make('autovideosink')
            self.pipe.add(q)
            self.pipe.add(conv)
            self.pipe.add(sink)
            self.pipe.sync_children_states()
            pad.link(q.get_static_pad('sink'))
            q.link(conv)
            conv.link(sink)
        elif name.startswith('audio'):
            q = Gst.ElementFactory.make('queue')
            conv = Gst.ElementFactory.make('audioconvert')
            resample = Gst.ElementFactory.make('audioresample')
            sink = Gst.ElementFactory.make('autoaudiosink')
            self.pipe.add(q)
            self.pipe.add(conv)
            self.pipe.add(resample)
            self.pipe.add(sink)
            self.pipe.sync_children_states()
            pad.link(q.get_static_pad('sink'))
            q.link(conv)
            conv.link(resample)
            resample.link(sink)

    def on_incoming_stream(self, _, pad):
        if pad.direction != Gst.PadDirection.SRC:
            return

        decodebin = Gst.ElementFactory.make('decodebin')
        decodebin.connect('pad-added', self.on_incoming_decodebin_stream)
        self.pipe.add(decodebin)
        decodebin.sync_state_with_parent()
        self.webrtc.link(decodebin)

    def start_pipeline(self):
        self.pipe = Gst.parse_launch(PIPELINE_DESC)
        self.webrtc = self.pipe.get_by_name('sendrecv')
        self.webrtc.connect('on-negotiation-needed', self.on_negotiation_needed)
        self.webrtc.connect('on-ice-candidate', self.send_ice_candidate_message)
        self.webrtc.connect('pad-added', self.on_incoming_stream)

        trans = self.webrtc.emit('get-transceiver', 0)
        if DO_RTX:
            trans.set_property ('do-nack', True)
        self.pipe.set_state(Gst.State.PLAYING)

    def extract_ice_from_sdp(self, sdp):
        mlineindex = -1
        for line in sdp.splitlines():
            if line.startswith("a=candidate"):
                candidate = line[2:]
                if mlineindex < 0:
                    print("Received ice candidate in SDP before any m= line")
                    continue
                print ('Received remote ice-candidate mlineindex {}: {}'.format(mlineindex, candidate))
                self.webrtc.emit('add-ice-candidate', mlineindex, candidate)
            elif line.startswith("m="):
                mlineindex += 1

    async def handle_sdp(self, msg):
        print (msg)
        if 'sdp' in msg:
            sdp = msg['sdp']
            assert(msg['type'] == 'answer')
            print ('Received answer:\n%s' % sdp)
            res, sdpmsg = GstSdp.SDPMessage.new_from_text(sdp)
            answer = GstWebRTC.WebRTCSessionDescription.new(GstWebRTC.WebRTCSDPType.ANSWER, sdpmsg)
            promise = Gst.Promise.new()
            self.webrtc.emit('set-remote-description', answer, promise)
            promise.interrupt()

            # Extract ICE candidates from the SDP to work around a GStreamer
            # limitation in (at least) 1.16.2 and below
            self.extract_ice_from_sdp (sdp)

        elif 'ice' in msg:
            ice = msg['ice']
            candidate = ice['candidate']
            sdpmlineindex = ice['sdpMLineIndex']
            self.webrtc.emit('add-ice-candidate', sdpmlineindex, candidate)

    async def loop(self):
        signaling = self.signaling
        await signaling.connect()
        await signaling.attach("janus.plugin.videoroom")

        loop = asyncio.get_event_loop()
        loop.create_task(signaling.keepalive())
        #asyncio.create_task(self.keepalive())

        joinmessage = { "request": "join", "ptype": "publisher", "room": 1234, "display": self.peer_id }
        await signaling.sendmessage(joinmessage)

        assert signaling.conn
        self.start_pipeline()

        while True:
            try:
                msg = await signaling.recv()
                if isinstance(msg, PluginData):
                    if msg.jsep is not None:
                        await self.handle_sdp(msg.jsep)
                elif isinstance(msg, Media):
                    print (msg)
                elif isinstance(msg, WebrtcUp):
                    print (msg)
                elif isinstance(msg, SlowLink):
                    print (msg)
                elif isinstance(msg, HangUp):
                    print (msg)
                elif not isinstance(msg, Ack):
                    if 'candidate' in msg:
                       ice = msg['candidate']
                       print (ice)
                       if 'candidate' in ice:
                           candidate = ice['candidate']
                           sdpmlineindex = ice['sdpMLineIndex']
                           self.webrtc.emit('add-ice-candidate', sdpmlineindex, candidate)
                    print(msg)
            except (KeyboardInterrupt, ConnectionClosed):
                return

        return 0

    async def close(self):
        return await self.signaling.close()

def check_plugins():
    needed = ["opus", "vpx", "nice", "webrtc", "dtls", "srtp", "rtp",
              "rtpmanager", "videotestsrc", "audiotestsrc"]
    missing = list(filter(lambda p: Gst.Registry.get().find_plugin(p) is None, needed))
    if len(missing):
        print('Missing gstreamer plugins:', missing)
        return False
    return True


if __name__=='__main__':
    Gst.init(None)
    if not check_plugins():
        sys.exit(1)
    parser = argparse.ArgumentParser()
    parser.add_argument('label', help='videoroom label')
    parser.add_argument('--server', help='Signalling server to connect to, eg "wss://127.0.0.1:8989"')
    args = parser.parse_args()
    c = WebRTCClient(args.label, args.server)
    loop = asyncio.get_event_loop()
    try:
        loop.run_until_complete(
            c.loop()
        )
    except KeyboardInterrupt:
        pass
    finally:
        print("Interrupted, cleaning up")
        loop.run_until_complete(c.close())
