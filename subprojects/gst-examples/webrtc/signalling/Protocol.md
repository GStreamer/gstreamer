## Terminology

### Client

A GStreamer-based application

### Browser

A JS application that runs in the browser and uses built-in browser webrtc APIs

### Peer
 
Any webrtc-using application that can participate in a call

### Signalling server

Basic websockets server implemented in Python that manages the peers list and shovels data between peers

## Overview

This is a basic protocol for doing 1-1 audio+video calls between a gstreamer app and a JS app in a browser.

## Peer registration

Peers must register with the signalling server before a call can be initiated. The server connection should stay open as long as the peer is available or in a call.

This protocol builds upon https://github.com/shanet/WebRTC-Example/

* Connect to the websocket server
* Send `HELLO <uid>` where `<uid>` is a string which will uniquely identify this peer
* Receive `HELLO`
* Any other message starting with `ERROR` is an error.

### 1-1 calls with a 'session'

* To connect to a single peer, send `SESSION <uid>` where `<uid>` identifies the peer to connect to, and receive `SESSION_OK`
* All further messages will be forwarded to the peer
* The call negotiation with the peer can be started by sending JSON encoded SDP (the offer) and ICE
* You can also ask the peer to send the SDP offer and begin sending ICE candidates. After `SESSION_OK` if you send `OFFER_REQUEST`, the peer will take over. (NEW in 1.19, not all clients support this)

* Closure of the server connection means the call has ended; either because the other peer ended it or went away
* To end the call, disconnect from the server. You may reconnect again whenever you wish.

### Multi-party calls with a 'room'

* To create a multi-party call, you must first register (or join) a room. Send `ROOM <room_id>` where `<room_id>` is a unique room name
* Receive `ROOM_OK ` from the server if this is a new room, or `ROOM_OK <peer1_id> <peer2_id> ...` where `<peerN_id>` are unique identifiers for the peers already in the room
* To send messages to a specific peer within the room for call negotiation (or any other purpose, use `ROOM_PEER_MSG <peer_id> <msg>`
* When a new peer joins the room, you will receive a `ROOM_PEER_JOINED <peer_id>` message
 - For the purposes of convention and to avoid overwhelming newly-joined peers, offers must only be sent by the newly-joined peer
* When a peer leaves the room, you will receive a `ROOM_PEER_LEFT <peer_id>` message
  - You should stop sending/receiving media from/to this peer
* To get a list of all peers currently in the room, send `ROOM_PEER_LIST` and receive `ROOM_PEER_LIST <peer1_id> ...`
  - This list will never contain your own `<uid>`
  - In theory you should never need to use this since you are guaranteed to receive JOINED and LEFT messages for all peers in a room
* You may stay connected to a room for as long as you like

## Negotiation

Once a call has been setup with the signalling server, the peers must negotiate SDP and ICE candidates with each other.

The calling side must create an SDP offer and send it to the peer as a JSON object:

```json
{
    "sdp": {
                "sdp": "o=- [....]",
                "type": "offer"
    }
}
```

The callee must then reply with an answer:

```json
{
    "sdp": {
                "sdp": "o=- [....]",
                "type": "answer"
    }
}
```

ICE candidates must be exchanged similarly by exchanging JSON objects:


```json
{
    "ice": {
                "candidate": ...,
                "sdpMLineIndex": ...,
                ...
    }
}
```

Note that the structure of these is the same as that specified by the WebRTC spec.
