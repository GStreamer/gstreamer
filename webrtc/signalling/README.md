## Overview

Read Protocol.md

## Dependencies

* Python 3
* pip3 install --user websockets

## Example usage

In three separate tabs, run consecutively:

```console
$ ./generate_certs.sh
$ ./simple-server.py
```

```console
$ ./client.py
Our uid is 'ws-test-client-8f63b9'
```

```console
$ ./client.py --call ws-test-client-8f63b9
```
