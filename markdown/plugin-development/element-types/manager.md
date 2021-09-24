---
title: Writing a Manager
...

# Writing a Manager

Managers are elements that add a function or unify the function of
another (series of) element(s). Managers are generally a `GstBin` with
one or more ghostpads. Inside them is/are the actual element(s) that
matters. There is several cases where this is useful. For example:

  - To add support for private events with custom event handling to
    another element.

  - To add support for custom pad `_query ()` or `_convert ()` handling
    to another element.

  - To add custom data handling before or after another element's data
    handler function (generally its `_chain ()` function).

  - To embed an element, or a series of elements, into something that
    looks and works like a simple element to the outside world. This is
    particular handy for implementing sources and sink elements with
    multiple pads.

Making a manager is about as simple as it gets. You can derive from a
`GstBin`, and in most cases, you can embed the required elements in the
`_init ()` already, including setup of ghostpads. If you need any custom
data handlers, you can connect signals or embed a second element which
you control.
