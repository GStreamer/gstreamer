# Terminology

### Client

A GStreamer-based application

### Browser

A JS application that runs in the browser and uses built-in browser webrtc APIs

### Peer
 
Any webrtc-using application that can participate in a call

### Signalling server

Basic websockets server implemented in Python that manages the peers list and shovels data between peers

# Overview

This is a basic protocol for doing 1-1 audio+video calls between a gstreamer app and a JS app in a browser.

# Peer registration and calling

Peers must register with the signalling server before a call can be initiated. The server connection should stay open as long as the peer is available or in a call.

This protocol builds upon https://github.com/shanet/WebRTC-Example/

* Connect to the websocket server
* Send `HELLO <uid>` where `<uid>` is a string which will uniquely identify this peer
* Receive `HELLO`
* Any other message starting with `ERROR` is an error.

* To connect to a peer, send `SESSION <uid>` where `<uid>` identifies the peer to connect to, and receive `SESSION_OK`
* All further messages will be forwarded to the peer
* The call negotiation with the peer can be started by sending JSON encoded SDP and ICE

* Closure of the server connection means the call has ended; either because the other peer ended it or went away
* To end the call, disconnect from the server. You may reconnect again whenever you wish.

# Negotiation

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
