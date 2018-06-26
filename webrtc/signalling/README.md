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

```console
$ ./session-client.py
Our uid is 'ws-test-client-8f63b9'
```

```console
$ ./session-client.py --call ws-test-client-8f63b9
```
