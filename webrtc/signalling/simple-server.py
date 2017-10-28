#!/usr/bin/env python3
#
# Example 1-1 call signalling server
#
# Copyright (C) 2017 Centricular Ltd.
#
#  Author: Nirbheek Chauhan <nirbheek@centricular.com>
#

import os
import sys
import ssl
import logging
import asyncio
import websockets
import argparse

from concurrent.futures._base import TimeoutError

parser = argparse.ArgumentParser(formatter_class=argparse.ArgumentDefaultsHelpFormatter)
parser.add_argument('--addr', default='0.0.0.0', help='Address to listen on')
parser.add_argument('--port', default=8443, type=int, help='Port to listen on')
parser.add_argument('--keepalive-timeout', dest='keepalive_timeout', default=30, type=int, help='Timeout for keepalive (in seconds)')
parser.add_argument('--cert-path', default=os.path.dirname(__file__))

options = parser.parse_args(sys.argv[1:])

ADDR_PORT = (options.addr, options.port)
KEEPALIVE_TIMEOUT = options.keepalive_timeout

############### Global data ###############

# Format: {uid: (Peer WebSocketServerProtocol,
#                remote_address,
#                <'session'|room_id|None>)}
peers = dict()
# Format: {caller_uid: callee_uid,
#          callee_uid: caller_uid}
# Bidirectional mapping between the two peers
sessions = dict()
# Format: {room_id: {peer1_id, peer2_id, peer3_id, ...}}
# Room dict with a set of peers in each room
rooms = dict()

############### Helper functions ###############

async def recv_msg_ping(ws, raddr):
    '''
    Wait for a message forever, and send a regular ping to prevent bad routers
    from closing the connection.
    '''
    msg = None
    while msg is None:
        try:
            msg = await asyncio.wait_for(ws.recv(), KEEPALIVE_TIMEOUT)
        except TimeoutError:
            print('Sending keepalive ping to {!r} in recv'.format(raddr))
            await ws.ping()
    return msg

async def disconnect(ws, peer_id):
    '''
    Remove @peer_id from the list of sessions and close our connection to it.
    This informs the peer that the session and all calls have ended, and it
    must reconnect.
    '''
    global sessions
    if peer_id in sessions:
        del sessions[peer_id]
    # Close connection
    if ws and ws.open:
        # Don't care about errors
        asyncio.ensure_future(ws.close(reason='hangup'))

async def cleanup_session(uid):
    if uid in sessions:
        other_id = sessions[uid]
        del sessions[uid]
        print("Cleaned up {} session".format(uid))
        if other_id in sessions:
            del sessions[other_id]
            print("Also cleaned up {} session".format(other_id))
            # If there was a session with this peer, also
            # close the connection to reset its state.
            if other_id in peers:
                print("Closing connection to {}".format(other_id))
                wso, oaddr, _ = peers[other_id]
                del peers[other_id]
                await wso.close()

async def cleanup_room(uid, room_id):
    room_peers = rooms[room_id]
    if uid not in room_peers:
        return
    room_peers.remove(uid)
    for pid in room_peers:
        wsp, paddr, _ = peers[pid]
        msg = 'ROOM_PEER_LEFT {}'.format(uid)
        print('room {}: {} -> {}: {}'.format(room_id, uid, pid, msg))
        await wsp.send(msg)

async def remove_peer(uid):
    await cleanup_session(uid)
    if uid in peers:
        ws, raddr, status = peers[uid]
        if status and status != 'session':
            await cleanup_room(uid, status)
        del peers[uid]
        await ws.close()
        print("Disconnected from peer {!r} at {!r}".format(uid, raddr))

############### Handler functions ###############

async def connection_handler(ws, uid):
    global peers, sessions, rooms
    raddr = ws.remote_address
    peer_status = None
    peers[uid] = [ws, raddr, peer_status]
    print("Registered peer {!r} at {!r}".format(uid, raddr))
    while True:
        # Receive command, wait forever if necessary
        msg = await recv_msg_ping(ws, raddr)
        # Update current status
        peer_status = peers[uid][2]
        # We are in a session or a room, messages must be relayed
        if peer_status is not None:
            # We're in a session, route message to connected peer
            if peer_status == 'session':
                other_id = sessions[uid]
                wso, oaddr, status = peers[other_id]
                assert(status == 'session')
                print("{} -> {}: {}".format(uid, other_id, msg))
                await wso.send(msg)
            # We're in a room, accept room-specific commands
            elif peer_status:
                # ROOM_PEER_MSG peer_id MSG
                if msg.startswith('ROOM_PEER_MSG'):
                    _, other_id, msg = msg.split(maxsplit=2)
                    if other_id not in peers:
                        await ws.send('ERROR peer {!r} not found'
                                      ''.format(other_id))
                        continue
                    wso, oaddr, status = peers[other_id]
                    if status != room_id:
                        await ws.send('ERROR peer {!r} is not in the room'
                                      ''.format(other_id))
                        continue
                    msg = 'ROOM_PEER_MSG {} {}'.format(uid, msg)
                    print('room {}: {} -> {}: {}'.format(room_id, uid, other_id, msg))
                    await wso.send(msg)
                elif msg == 'ROOM_PEER_LIST':
                    room_id = peers[peer_id][2]
                    room_peers = ' '.join([pid for pid in rooms[room_id] if pid != peer_id])
                    msg = 'ROOM_PEER_LIST {}'.format(room_peers)
                    print('room {}: -> {}: {}'.format(room_id, uid, msg))
                    await ws.send(msg)
                else:
                    await ws.send('ERROR invalid msg, already in room')
                    continue
            else:
                raise AssertionError('Unknown peer status {!r}'.format(peer_status))
        # Requested a session with a specific peer
        elif msg.startswith('SESSION'):
            print("{!r} command {!r}".format(uid, msg))
            _, callee_id = msg.split(maxsplit=1)
            if callee_id not in peers:
                await ws.send('ERROR peer {!r} not found'.format(callee_id))
                continue
            if peer_status is not None:
                await ws.send('ERROR peer {!r} busy'.format(callee_id))
                continue
            await ws.send('SESSION_OK')
            wsc = peers[callee_id][0]
            print('Session from {!r} ({!r}) to {!r} ({!r})'
                  ''.format(uid, raddr, callee_id, wsc.remote_address))
            # Register session
            peers[uid][2] = peer_status = 'session'
            sessions[uid] = callee_id
            peers[callee_id][2] = 'session'
            sessions[callee_id] = uid
        # Requested joining or creation of a room
        elif msg.startswith('ROOM'):
            print('{!r} command {!r}'.format(uid, msg))
            _, room_id = msg.split(maxsplit=1)
            # Room name cannot be 'session', empty, or contain whitespace
            if room_id == 'session' or room_id.split() != [room_id]:
                await ws.send('ERROR invalid room id {!r}'.format(room_id))
                continue
            if room_id in rooms:
                if uid in rooms[room_id]:
                    raise AssertionError('How did we accept a ROOM command '
                                         'despite already being in a room?')
            else:
                # Create room if required
                rooms[room_id] = set()
            room_peers = ' '.join([pid for pid in rooms[room_id]])
            await ws.send('ROOM_OK {}'.format(room_peers))
            # Enter room
            peers[uid][2] = peer_status = room_id
            rooms[room_id].add(uid)
            for pid in rooms[room_id]:
                if pid == uid:
                    continue
                wsp, paddr, _ = peers[pid]
                msg = 'ROOM_PEER_JOINED {}'.format(uid)
                print('room {}: {} -> {}: {}'.format(room_id, uid, pid, msg))
                await wsp.send(msg)
        else:
            print('Ignoring unknown message {!r} from {!r}'.format(msg, uid))

async def hello_peer(ws):
    '''
    Exchange hello, register peer
    '''
    raddr = ws.remote_address
    hello = await ws.recv()
    hello, uid = hello.split(maxsplit=1)
    if hello != 'HELLO':
        await ws.close(code=1002, reason='invalid protocol')
        raise Exception("Invalid hello from {!r}".format(raddr))
    if not uid or uid in peers or uid.split() != [uid]: # no whitespace
        await ws.close(code=1002, reason='invalid peer uid')
        raise Exception("Invalid uid {!r} from {!r}".format(uid, raddr))
    # Send back a HELLO
    await ws.send('HELLO')
    return uid

async def handler(ws, path):
    '''
    All incoming messages are handled here. @path is unused.
    '''
    raddr = ws.remote_address
    print("Connected to {!r}".format(raddr))
    peer_id = await hello_peer(ws)
    try:
        await connection_handler(ws, peer_id)
    except websockets.ConnectionClosed:
        print("Connection to peer {!r} closed, exiting handler".format(raddr))
    finally:
        await remove_peer(peer_id)

# Create an SSL context to be used by the websocket server
certpath = options.cert_path
print('Using TLS with keys in {!r}'.format(certpath))
if 'letsencrypt' in certpath:
    chain_pem = os.path.join(certpath, 'fullchain.pem')
    key_pem = os.path.join(certpath, 'privkey.pem')
else:
    chain_pem = os.path.join(certpath, 'cert.pem')
    key_pem = os.path.join(certpath, 'key.pem')

sslctx = ssl.create_default_context()
try:
    sslctx.load_cert_chain(chain_pem, keyfile=key_pem)
except FileNotFoundError:
    print("Certificates not found, did you run generate_cert.sh?")
    sys.exit(1)
# FIXME
sslctx.check_hostname = False
sslctx.verify_mode = ssl.CERT_NONE

print("Listening on https://{}:{}".format(*ADDR_PORT))
# Websocket server
wsd = websockets.serve(handler, *ADDR_PORT, ssl=sslctx,
                       # Maximum number of messages that websockets will pop
                       # off the asyncio and OS buffers per connection. See:
                       # https://websockets.readthedocs.io/en/stable/api.html#websockets.protocol.WebSocketCommonProtocol
                       max_queue=16)

logger = logging.getLogger('websockets.server')

logger.setLevel(logging.ERROR)
logger.addHandler(logging.StreamHandler())

asyncio.get_event_loop().run_until_complete(wsd)
asyncio.get_event_loop().run_forever()
