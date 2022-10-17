#!/usr/bin/env python3
#
# Test client for simple room-based multi-peer p2p calling
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
parser.add_argument('--room', default=None, help='the room to join')

options = parser.parse_args(sys.argv[1:])

SERVER_ADDR = options.url
PEER_ID = 'ws-test-client-' + str(uuid.uuid4())[:6]
ROOM_ID = options.room
if ROOM_ID is None:
    print('--room argument is required')
    sys.exit(1)

sslctx = False
if SERVER_ADDR.startswith(('wss://', 'https://')):
    sslctx = ssl.create_default_context()
    # FIXME
    sslctx.check_hostname = False
    sslctx.verify_mode = ssl.CERT_NONE

def get_answer_sdp(offer, peer_id):
    # Here we'd parse the incoming JSON message for ICE and SDP candidates
    print("Got: " + offer)
    sdp = json.dumps({'sdp': 'reply sdp'})
    answer = 'ROOM_PEER_MSG {} {}'.format(peer_id, sdp)
    print("Sent: " + answer)
    return answer

def get_offer_sdp(peer_id):
    sdp = json.dumps({'sdp': 'initial sdp'})
    offer = 'ROOM_PEER_MSG {} {}'.format(peer_id, sdp)
    print("Sent: " + offer)
    return offer

async def hello():
    async with websockets.connect(SERVER_ADDR, ssl=sslctx) as ws:
        await ws.send('HELLO ' + PEER_ID)
        assert(await ws.recv() == 'HELLO')

        await ws.send('ROOM {}'.format(ROOM_ID))

        sent_offers = set()
        # Receive messages
        while True:
            msg = await ws.recv()
            if msg.startswith('ERROR'):
                # On error, we bring down the webrtc pipeline, etc
                print('{!r}, exiting'.format(msg))
                return
            if msg.startswith('ROOM_OK'):
                print('Got ROOM_OK for room {!r}'.format(ROOM_ID))
                _, *room_peers = msg.split()
                for peer_id in room_peers:
                    print('Sending offer to {!r}'.format(peer_id))
                    # Create a peer connection for each peer and start
                    # exchanging SDP and ICE candidates
                    await ws.send(get_offer_sdp(peer_id))
                    sent_offers.add(peer_id)
                continue
            elif msg.startswith('ROOM_PEER'):
                if msg.startswith('ROOM_PEER_JOINED'):
                    _, peer_id = msg.split(maxsplit=1)
                    print('Peer {!r} joined the room'.format(peer_id))
                    # Peer will send us an offer
                    continue
                if msg.startswith('ROOM_PEER_LEFT'):
                    _, peer_id = msg.split(maxsplit=1)
                    print('Peer {!r} left the room'.format(peer_id))
                    continue
                elif msg.startswith('ROOM_PEER_MSG'):
                    _, peer_id, msg = msg.split(maxsplit=2)
                    if peer_id in sent_offers:
                        print('Got answer from {!r}: {}'.format(peer_id, msg))
                        continue
                    print('Got offer from {!r}, replying'.format(peer_id))
                    await ws.send(get_answer_sdp(msg, peer_id))
                    continue
            print('Unknown msg: {!r}, exiting'.format(msg))
            return

print('Our uid is {!r}'.format(PEER_ID))

try:
    asyncio.run(hello())
except websockets.exceptions.InvalidHandshake:
    print('Invalid handshake: are you sure this is a websockets server?\n')
    raise
except ssl.SSLError:
    print('SSL Error: are you sure the server is using TLS?\n')
    raise
