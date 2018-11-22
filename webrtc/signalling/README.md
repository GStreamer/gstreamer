## Overview

Read Protocol.md

## Dependencies

* Python 3
* pip3 install --user websockets

## Example usage

In three separate tabs, run consecutively:

```console
$ ./generate_cert.sh
$ ./simple-server.py
```

### Session Based

```console
$ ./session-client.py
Our uid is 'ws-test-client-8f63b9'
```

```console
$ ./session-client.py --call ws-test-client-8f63b9
```
### Room Based

```console
$ ./room-client.py --room 123
Our uid is 'ws-test-client-bdb5b9'
Got ROOM_OK for room '123'
```

Another window

```console
$ ./room-client.py --room 123
Our uid is 'ws-test-client-78b59a'
Got ROOM_OK for room '123'
Sending offer to 'ws-test-client-bdb5b9'
Sent: ROOM_PEER_MSG ws-test-client-bdb5b9 {"sdp": "initial sdp"}
Got answer from 'ws-test-client-bdb5b9': {"sdp": "reply sdp"}
```

.. and similar output with more clients in the same room.
