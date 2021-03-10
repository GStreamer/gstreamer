## Overview

Read Protocol.md

## Dependencies

* Python 3
* pip3 install --user websockets

## Example usage

For dev usage, generate a self-signed certificate and run the server:

```console
$ ./generate_cert.sh
$ ./simple_server.py
```

If you want to use this server from the browser (to use the JS example, for
instance), you will need to go to `https://127.0.0.1:8443` and accept the
self-signed certificate. This step is not required if you will be deploying on
a server with a CA-signed certificate, in which case you should use
`./simple_server.py --cert-path <cert path>`.

### Session Based

In two new consoles, run these two commands:

```console
$ ./session-client.py
Our uid is 'ws-test-client-8f63b9'
```

```console
$ ./session-client.py --call ws-test-client-8f63b9
```
### Room Based

Or, if you want to test rooms, run these two in two new consoles:

```console
$ ./room-client.py --room 123
Our uid is 'ws-test-client-bdb5b9'
Got ROOM_OK for room '123'
```

```console
$ ./room-client.py --room 123
Our uid is 'ws-test-client-78b59a'
Got ROOM_OK for room '123'
Sending offer to 'ws-test-client-bdb5b9'
Sent: ROOM_PEER_MSG ws-test-client-bdb5b9 {"sdp": "initial sdp"}
Got answer from 'ws-test-client-bdb5b9': {"sdp": "reply sdp"}
```

You will see similar output with more clients in the same room.
