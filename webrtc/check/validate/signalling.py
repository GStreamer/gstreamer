# Copyright (c) 2020, Matthew Waters <matthew@centricular.com>
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this program; if not, write to the
# Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
# Boston, MA 02110-1301, USA.

import websockets
import asyncio
import ssl
import os
import sys
import threading
import json
import logging

from observer import Signal, StateObserver, WebRTCObserver, DataChannelObserver
from enums import SignallingState, NegotiationState, DataChannelState

l = logging.getLogger(__name__)

class AsyncIOThread(threading.Thread):
    """
    Run an asyncio loop in another thread.
    """
    def __init__ (self, loop):
        threading.Thread.__init__(self)
        self.loop = loop

    def run(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def stop_thread(self):
        self.loop.call_soon_threadsafe(self.loop.stop)


class SignallingClientThread(object):
    """
    Connect to a signalling server
    """
    def __init__(self, server):
        # server string to connect to.  Passed directly to websockets.connect()
        self.server = server

        # fired after we have connected to the signalling server
        self.wss_connected = Signal()
        # fired every time we receive a message from the signalling server
        self.message = Signal()

        self._init_async()

    def _init_async(self):
        self._running = False
        self.conn = None
        self._loop = asyncio.new_event_loop()

        self._thread = AsyncIOThread(self._loop)
        self._thread.start()

        self._loop.call_soon_threadsafe(lambda: asyncio.ensure_future(self._a_loop()))

    async def _a_connect(self):
        # connect to the signalling server
        assert not self.conn
        sslctx = ssl.create_default_context(purpose=ssl.Purpose.CLIENT_AUTH)
        self.conn = await websockets.connect(self.server, ssl=sslctx)

    async def _a_loop(self):
        self._running = True
        l.info('loop started')
        await self._a_connect()
        self.wss_connected.fire()
        assert self.conn
        async for message in self.conn:
            self.message.fire(message)
        l.info('loop exited')

    def send(self, data):
        # send some information to the peer
        async def _a_send():
            await self.conn.send(data)
        self._loop.call_soon_threadsafe(lambda: asyncio.ensure_future(_a_send()))

    def stop(self):
        if self._running == False:
            return

        cond = threading.Condition()

        # asyncio, why you so complicated to stop ?
        tasks = asyncio.all_tasks(self._loop)
        async def _a_stop():
            if self.conn:
                await self.conn.close()
            self.conn = None

            to_wait = [t for t in tasks if not t.done()]
            if to_wait:
                l.info('waiting for ' + str(to_wait))
                done, pending = await asyncio.wait(to_wait)
            with cond:
                l.error('notifying cond')
                cond.notify()
            self._running = False

        with cond:
            self._loop.call_soon_threadsafe(lambda: asyncio.ensure_future(_a_stop()))
            l.error('cond waiting')
            cond.wait()
            l.error('cond waited')
        self._thread.stop_thread()
        self._thread.join()
        l.error('thread joined')


class WebRTCSignallingClient(SignallingClientThread):
    """
    Signalling client implementation.  Deals wit session management over the
    signalling protocol.  Sends and receives from a peer.
    """
    def __init__(self, server, id_):
        super().__init__(server)

        self.wss_connected.connect(self._on_connection)
        self.message.connect(self._on_message)
        self.state = SignallingState.NEW
        self._state_observer = StateObserver(self, "state", threading.Condition())

        self.id = id_
        self._peerid = None

        # fired when the hello has been received
        self.connected = Signal()
        # fired when the signalling server responds that the session creation is ok
        self.session_created = Signal()
        # fired on an error
        self.error = Signal()
        # fired when the peer receives some json data
        self.have_json = Signal()

    def _update_state(self, new_state):
        self._state_observer.update (new_state)

    def wait_for_states(self, states):
        return self._state_observer.wait_for (states)

    def hello(self):
        self.send('HELLO ' + str(self.id))
        l.info("sent HELLO")
        self.wait_for_states([SignallingState.HELLO])

    def create_session(self, peerid):
        self._peerid = peerid
        self.send('SESSION {}'.format(self._peerid))
        l.info("sent SESSION")
        self.wait_for_states([SignallingState.SESSION])

    def _on_connection(self):
        self._update_state (SignallingState.OPEN)

    def _on_message(self, message):
        l.debug("received: " + message)
        if message == 'HELLO':
            self._update_state (SignallingState.HELLO)
            self.connected.fire()
        elif message == 'SESSION_OK':
            self._update_state (SignallingState.SESSION)
            self.session_created.fire()
        elif message.startswith('ERROR'):
            self._update_state (SignallingState.ERROR)
            self.error.fire(message)
        else:
            msg = json.loads(message)
            self.have_json.fire(msg)
        return False


class RemoteWebRTCObserver(WebRTCObserver):
    """
    Use information sent over the signalling channel to construct the current
    state of a remote peer. Allow performing actions by sending requests over
    the signalling channel.
    """
    def __init__(self, signalling):
        super().__init__()
        self.signalling = signalling

        def on_json(msg):
            if 'STATE' in msg:
                state = NegotiationState (msg['STATE'])
                self._update_negotiation_state(state)
                if state == NegotiationState.OFFER_CREATED:
                    self.on_offer_created.fire(msg['description'])
                elif state == NegotiationState.ANSWER_CREATED:
                    self.on_answer_created.fire(msg['description'])
                elif state == NegotiationState.OFFER_SET:
                    self.on_offer_set.fire (msg['description'])
                elif state == NegotiationState.ANSWER_SET:
                    self.on_answer_set.fire (msg['description'])
            elif 'DATA-NEW' in msg:
                new = msg['DATA-NEW']
                observer = RemoteDataChannelObserver(new['id'], new['location'], self)
                self.add_channel (observer)
            elif 'DATA-STATE' in msg:
                ident = msg['id']
                channel = self.find_channel(ident)
                channel._update_state (DataChannelState(msg['DATA-STATE']))
            elif 'DATA-MSG' in msg:
                ident = msg['id']
                channel = self.find_channel(ident)
                channel.got_message(msg['DATA-MSG'])
        self.signalling.have_json.connect (on_json)

    def add_data_channel (self, ident):
        msg = json.dumps({'DATA_CREATE': {'id': ident}})
        self.signalling.send (msg)

    def create_offer (self):
        msg = json.dumps({'CREATE_OFFER': ""})
        self.signalling.send (msg)

    def create_answer (self):
        msg = json.dumps({'CREATE_ANSWER': ""})
        self.signalling.send (msg)

    def set_title (self, title):
        # entirely for debugging purposes
        msg = json.dumps({'SET_TITLE': title})
        self.signalling.send (msg)

    def set_options (self, opts):
        options = {}
        if opts.has_field("remote-bundle-policy"):
            options["bundlePolicy"] = opts["remote-bundle-policy"]
        msg = json.dumps({'OPTIONS' : options})
        self.signalling.send (msg)


class RemoteDataChannelObserver(DataChannelObserver):
    """
    Use information sent over the signalling channel to construct the current
    state of a remote peer's data channel. Allow performing actions by sending
    requests over the signalling channel.
    """
    def __init__(self, ident, location, webrtc):
        super().__init__(ident, location)
        self.webrtc = webrtc

    def send_string(self, msg):
        msg = json.dumps({'DATA_SEND_MSG': {'msg' : msg, 'id': self.ident}})
        self.webrtc.signalling.send (msg)

    def close (self):
        msg = json.dumps({'DATA_CLOSE': {'id': self.ident}})
        self.webrtc.signalling.send (msg)
