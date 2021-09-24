# Progress Reporting

This document describes the design and use cases for the progress
reporting messages.

PROGRESS messages are posted on the bus to inform the application about
the progress of asynchronous operations in the pipeline. This should not
be confused with asynchronous state changes.

We accommodate for the following requirements:

  - Application is informed when an async operation starts and
    completes.

  - It should be possible for the application to generically detect
    common operations and incorporate their progress into the GUI.

  - Applications can cancel pending operations by doing regular state
    changes.

  - Applications should be able to wait for completion of async
    operations.

We allow for the following scenarios:

  - Elements want to inform the application about asynchronous DNS
    lookups and pending network requests. This includes starting and
    completing the lookup.

  - Elements opening devices and resources asynchronously.

  - Applications having more freedom to implement timeout and
    cancelation of operations that currently block the state changes or
    happen invisibly behind the scenes.

## Rationale

The main reason for adding these extra progress notifications is
twofold:

### To give the application more information of what is going on

When there are well defined progress information codes, applications
can let the user know about the status of the progress. We anticipate to
have at least DNS resolving and server connections and requests be well
defined.

### To make the state changes non-blocking and cancellable.

Currently state changes such as going to the READY or PAUSED state often do
blocking calls such as resolving DNS or connecting to a remote server. These
operations often block the main thread and are often not cancellable, causing
application lockups.

We would like to make the state change function, instead, start a separate
thread that performs the blocking operations in a cancellable way. When going
back to the NULL state, all pending operations would be canceled immediately.

For downward state changes, we want to let the application implement its own
timeout mechanism. For example: when stopping an RTSP stream, the clients
needs to send a TEARDOWN request to the server. This can however take an
unlimited amount of time in case of network problems. We want to give the
application an opportunity to wait (and timeout) for the completion of the
async operation before setting the element to the final NULL state.

Progress updates are very similar to buffering messages in the same way
that the application can decide to wait for the completion of the
buffering process before performing the next state change. It might make
sense to implement buffering with the progress messages in the future.

## Async state changes

GStreamer currently has a `GST_STATE_CHANGE_ASYNC` return value to note
to the application that a state change is happening asynchronously.

The main purpose of this return value is to make the pipeline wait for
preroll and delay a future (upwards) state changes until the sinks are
prerolled.

In the case of async operations on source, this will automatically force
sinks to stay async because they will not preroll before the source can
produce data.

The fact that other asynchronous operations happen behind the scenes is
irrelevant for the prerolling process so it is not implemented with the
ASYNC state change return value in order to not complicate the state
changes and mix concepts.

## Use cases

### RTSP client (but also HTTP, MMS, …)

When the client goes from the READY to the PAUSED state, it opens a socket,
performs a DNS lookup, retrieves the SDP and negotiates the streams. All these
operations currently block the state change function for an indefinite amount
of time and while they are blocking cannot be canceled.

Instead, a thread would be started to perform these operations asynchronously
and the state change would complete with the usual `NO_PREROLL` return value.
Before starting the thread a PROGRESS message would be posted to mark the
start of the async operation.

As the DNS lookup completes and the connection is established, PROGRESS
messages are posted on the bus to inform the application of the progress. When
something fails, an error is posted and a PROGRESS CANCELED message is posted.
The application can then stop the pipeline.

If there are no errors and the setup of the streams completed successfully, a
PROGRESS COMPLETED is posted on the bus. The thread then goes to sleep and the
asynchronous operation completed.

The RTSP protocol requires to send a TEARDOWN request to the server
before closing the connection and destroying the socket. A state change to the
READY state will issue the TEARDOWN request in the background and notify the
application of this pending request with a PROGRESS message.

The application might want to only go to the NULL state after it got confirmation
that the TEARDOWN request completed or it might choose to go to NULL after a
timeout. It might also be possible that the application just want to close the
socket as fast as possible without waiting for completion of the TEARDOWN request.

### Network performance measuring

DNS lookup and connection times can be measured by calculating the elapsed
time between the various PROGRESS messages.

## Messages

A new `PROGRESS` message will be created.

The following fields will be contained in the message:

- **`type`**, `GST_TYPE_PROGRESS_TYPE`: A set of types to define the type of progress
    * `GST_PROGRESS_TYPE_START`: A new task is started in the background
    * `GST_PROGRESS_TYPE_CONTINUE`: The previous tasks completed and a new one
    continues. This is done so that the application can follow a set of
    continuous tasks and react to COMPLETE only when the element completely
    finished.
    * `GST_PROGRESS_TYPE_CANCELED`: A task is canceled by the user.
    * `GST_PROGRESS_TYPE_ERROR`: A task stopped because of an error. In case of
    an error, an error message will have been posted before.
    * `GST_PROGRESS_TYPE_COMPLETE`: A task completed successfully.

- **`code`**, `G_TYPE_STRING`: A generic extensible string that can be used to
programmatically determine the action that is in progress. Some standard
predefined codes will be defined.

- **`text`**, `G_TYPE_STRING`: A user visible string detailing the action.

- **`percent`**, `G_TYPE_INT`: between 0 and 100 Progress of the action as
a percentage, the following values are allowed:
    - `GST_PROGRESS_TYPE_START` always has a 0% value.
    - `GST_PROGRESS_TYPE_CONTINUE` have a value between 0 and 100
    - `GST_PROGRESS_TYPE_CANCELED`, `GST_PROGRESS_TYPE_ERROR` and
    `GST_PROGRESS_TYPE_COMPLETE` always have a 100% value.

- **`timeout`**, `G_TYPE_INT` in milliseconds: The timeout of the async
operation. -1 if unknown/unlimited.. This field can be interesting to the
application when it wants to display some sort of progress indication.

- ….

Depending on the code, more fields can be put here.

## Implementation

Elements should not do blocking operations from the state change
function. Instead, elements should post an appropriate progress message
with the right code and of type `GST_PROGRESS_TYPE_START` and then
start a thread to perform the blocking calls in a cancellable manner.

It is highly recommended to only start async operations from the READY
to PAUSED state and onwards and not from the NULL to READY state. The
reason for this is that streaming threads are usually started in the
READY to PAUSED state and that the current NULL to READY state change is
used to perform a blocking check for the presence of devices.

The progress message needs to be posted from the state change function
so that the application can immediately take appropriate action after
setting the state.

The threads will usually perform many blocking calls with different
codes in a row, a client might first do a DNS query and then continue
with establishing a connection to the server. For this purpose the
`GST_PROGRESS_TYPE_CONTINUE` must be used.

Usually, the thread used to perform the blocking operations can be used
to implement the streaming threads when needed.

Upon downward state changes, operations that are busy in the thread are
canceled and `GST_PROGRESS_TYPE_CANCELED` is posted.

The application can know about pending tasks because they received the
`GST_PROGRESS_TYPE_START` messages that didn’t complete with a
`GST_PROGRESS_TYPE_COMPLETE` message, got canceled with a
`GST_PROGRESS_TYPE_CANCELED` or errored with
`GST_PROGRESS_TYPE_ERROR.` Applications should be able to choose if
they wait for the pending operation or cancel them.

If an async operation fails, an error message is posted first before the
`GST_PROGRESS_TYPE_ERROR` progress message.

## Categories

We want to propose some standard codes here:

* "open" : A resource is being opened

* "close" : A resource is being closed

* "name-lookup" : A DNS lookup.

* "connect" : A socket connection is established

* "disconnect" : a socket connection is closed

* "request" : A request is sent to a server and we are waiting for a reply.
This message is posted right before the request is sent and completed when the
reply has arrived completely. * "mount" : A volume is being mounted

* "unmount" : A volume is being unmounted

More codes can be posted by elements and can be made official later.
