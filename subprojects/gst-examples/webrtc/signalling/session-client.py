#!/usr/bin/env python3
#
# Test client for simple 1-1 call signalling server
#
# Copyright (C) 2017 Centricular Ltd.
#
#  Author: Nirbheek Chauhan <nirbheek@centricular.com>
#

import sys
import ssl
import json
import uuid
import asyncio
import websockets
import argparse

parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument('--url', default='wss://localhost:8443', help='URL to connect to')
parser.add_argument('--call', default=None, help='uid of peer to call')

options = parser.parse_args(sys.argv[1:])

SERVER_ADDR = options.url
CALLEE_ID = options.call
PEER_ID = 'ws-test-client-' + str(uuid.uuid4())[:6]

sslctx = False
if SERVER_ADDR.startswith(('wss://', 'https://')):
    sslctx = ssl.create_default_context()
    # FIXME
    sslctx.check_hostname = False
    sslctx.verify_mode = ssl.CERT_NONE

def reply_sdp_ice(msg):
    # Here we'd parse the incoming JSON message for ICE and SDP candidates
    print("Got: " + msg)
    reply = json.dumps({'sdp': 'reply sdp'})
    print("Sent: " + reply)
    return reply

def send_sdp_ice():
    reply = json.dumps({'sdp': 'initial sdp'})
    print("Sent: " + reply)
    return reply

async def hello():
    async with websockets.connect(SERVER_ADDR, ssl=sslctx) as ws:
        await ws.send('HELLO ' + PEER_ID)
        assert(await ws.recv() == 'HELLO')

        # Initiate call if requested
        if CALLEE_ID:
            await ws.send('SESSION {}'.format(CALLEE_ID))

        # Receive messages
        sent_sdp = False
        while True:
            msg = await ws.recv()
            if msg.startswith('ERROR'):
                # On error, we bring down the webrtc pipeline, etc
                print('{!r}, exiting'.format(msg))
                return
            if sent_sdp:
                print('Got reply sdp: ' + msg)
                return # Done
            if CALLEE_ID:
                if msg == 'SESSION_OK':
                    await ws.send(send_sdp_ice())
                    sent_sdp = True
                else:
                    print('Unknown reply: {!r}, exiting'.format(msg))
                    return
            else:
                await ws.send(reply_sdp_ice(msg))
                return # Done

print('Our uid is {!r}'.format(PEER_ID))

try:
    asyncio.run(hello())
except websockets.exceptions.InvalidHandshake:
    print('Invalid handshake: are you sure this is a websockets server?\n')
    raise
except ssl.SSLError:
    print('SSL Error: are you sure the server is using TLS?\n')
    raise
