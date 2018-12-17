#!/usr/bin/env python3
#
# Copyright (c) 2018, Matthew Waters <matthew@centricular.com>
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

from observer import Signal
from enums import SignallingState, RemoteState

class AsyncIOThread(threading.Thread):
    def __init__ (self, loop):
        threading.Thread.__init__(self)
        self.loop = loop

    def run(self):
        asyncio.set_event_loop(self.loop)
        self.loop.run_forever()

    def stop_thread(self):
        self.loop.call_soon_threadsafe(self.loop.stop)

class SignallingClientThread(object):
    def __init__(self, server):
        self.server = server

        self.wss_connected = Signal()
        self.message = Signal()

        self._init_async()

    def _init_async(self):
        self.conn = None
        self._loop = asyncio.new_event_loop()

        self._thread = AsyncIOThread(self._loop)
        self._thread.start()

        self._loop.call_soon_threadsafe(lambda: asyncio.ensure_future(self._a_loop()))

    async def _a_connect(self):
        assert not self.conn
        sslctx = ssl.create_default_context(purpose=ssl.Purpose.CLIENT_AUTH)
        self.conn = await websockets.connect(self.server, ssl=sslctx)

    async def _a_loop(self):
        await self._a_connect()
        self.wss_connected.fire()
        assert self.conn
        async for message in self.conn:
            self.message.fire(message)

    def send(self, data):
        async def _a_send():
            await self.conn.send(data)
        self._loop.call_soon_threadsafe(lambda: asyncio.ensure_future(_a_send()))

    def stop(self):
        cond = threading.Condition()

        # asyncio, why you so complicated to stop ?
        tasks = asyncio.all_tasks(self._loop)
        async def _a_stop():
            if self.conn:
                await self.conn.close()
            self.conn = None

            to_wait = [t for t in tasks if not t.done()]
            if to_wait:
                done, pending = await asyncio.wait(to_wait)
            with cond:
                cond.notify()

        self._loop.call_soon_threadsafe(lambda: asyncio.ensure_future(_a_stop()))
        with cond:
                cond.wait()
        self._thread.stop_thread()
        self._thread.join()

class WebRTCSignallingClient(SignallingClientThread):
    def __init__(self, server, id_):
        super().__init__(server)

        self.wss_connected.connect(self._on_connection)
        self.message.connect(self._on_message)
        self.state = SignallingState.NEW
        self._state_cond = threading.Condition()

        self.id = id_
        self._peerid = None

        # override that base class
        self.connected = Signal()
        self.session_created = Signal()
        self.error = Signal()
        self.have_json = Signal()

    def wait_for_states(self, states):
        ret = None
        with self._state_cond:
            while self.state not in states:
                self._state_cond.wait()
            ret = self.state
        return ret

    def _update_state(self, state):
        with self._state_cond:
            if self.state is not SignallingState.ERROR:
                self.state = state
            self._state_cond.notify_all()

    def hello(self):
        self.send('HELLO ' + str(self.id))
        self.wait_for_states([SignallingState.HELLO])
        print("signalling-client sent HELLO")

    def create_session(self, peerid):
        self._peerid = peerid
        self.send('SESSION {}'.format(self._peerid))
        self.wait_for_states([SignallingState.SESSION, SignallingState.ERROR])
        print("signalling-client sent SESSION")

    def _on_connection(self):
        self._update_state (SignallingState.OPEN)

    def _on_message(self, message):
        print("signalling-client received", message)
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

