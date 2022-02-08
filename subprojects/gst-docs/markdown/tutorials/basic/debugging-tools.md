# Basic tutorial 11: Debugging tools

## Goal

Sometimes things won’t go as expected and the error messages retrieved
from the bus (if any) just don’t provide enough information. Luckily,
GStreamer ships with massive amounts of debug information, which usually
hint what the problem might be. This tutorial shows:

  - How to get more debug information from GStreamer.

  - How to print your own debug information into the GStreamer log.

  - How to get pipeline graphs

## Printing debug information

### The debug log

GStreamer and its plugins are full of debug traces, this is, places in
the code where a particularly interesting piece of information is
printed to the console, along with time stamping, process, category,
source code file, function and element information.

The debug output is controlled with the `GST_DEBUG` environment
variable. Here’s an example with `GST_DEBUG=2`:

```
0:00:00.868050000  1592   09F62420 WARN                 filesrc gstfilesrc.c:1044:gst_file_src_start:<filesrc0> error: No such file "non-existing-file.webm"
```

As you can see, this is quite a bit of information. In fact, the
GStreamer debug log is so verbose, that when fully enabled it can render
applications unresponsive (due to the console scrolling) or fill up
megabytes of text files (when redirected to a file). For this reason,
the logs are categorized, and you seldom need to enable all categories
at once.

The first category is the Debug Level, which is a number specifying the
amount of desired output:

```
| # | Name    | Description                                                    |
|---|---------|----------------------------------------------------------------|
| 0 | none    | No debug information is output.                                |
| 1 | ERROR   | Logs all fatal errors. These are errors that do not allow the  |
|   |         | core or elements to perform the requested action. The          |
|   |         | application can still recover if programmed to handle the      |
|   |         | conditions that triggered the error.                           |
| 2 | WARNING | Logs all warnings. Typically these are non-fatal, but          |
|   |         | user-visible problems are expected to happen.                  |
| 3 | FIXME   | Logs all "fixme" messages. Those typically that a codepath that|
|   |         | is known to be incomplete has been triggered. It may work in   |
|   |         | most cases, but may cause problems in specific instances.      |
| 4 | INFO    | Logs all informational messages. These are typically used for  |
|   |         | events in the system that only happen once, or are important   |
|   |         | and rare enough to be logged at this level.                    |
| 5 | DEBUG   | Logs all debug messages. These are general debug messages for  |
|   |         | events that happen only a limited number of times during an    |
|   |         | object's lifetime; these include setup, teardown, change of    |
|   |         | parameters, etc.                                               |
| 6 | LOG     | Logs all log messages. These are messages for events that      |
|   |         | happen repeatedly during an object's lifetime; these include   |
|   |         | streaming and steady-state conditions. This is used for log    |
|   |         | messages that happen on every buffer in an element for example.|
| 7 | TRACE   | Logs all trace messages. Those are message that happen very    |
|   |         | very often. This is for example is each time the reference     |
|   |         | count of a GstMiniObject, such as a GstBuffer or GstEvent, is  |
|   |         | modified.                                                      |
| 9 | MEMDUMP | Logs all memory dump messages. This is the heaviest logging and|
|   |         | may include dumping the content of blocks of memory.           |
+------------------------------------------------------------------------------+
```

To enable debug output, set the `GST_DEBUG` environment variable to the
desired debug level. All levels below that will also be shown (i.e., if
you set `GST_DEBUG=2`, you will get both `ERROR` and
`WARNING` messages).

Furthermore, each plugin or part of the GStreamer defines its own
category, so you can specify a debug level for each individual category.
For example, `GST_DEBUG=2,audiotestsrc:6`, will use Debug Level 6 for
the `audiotestsrc` element, and 2 for all the others.

The `GST_DEBUG` environment variable, then, is a comma-separated list of
*category*:*level* pairs, with an optional *level* at the beginning,
representing the default debug level for all categories.

The `'*'` wildcard is also available. For example
`GST_DEBUG=2,audio*:6` will use Debug Level 6 for all categories
starting with the word `audio`. `GST_DEBUG=*:2` is equivalent to
`GST_DEBUG=2`.

Use `gst-launch-1.0 --gst-debug-help` to obtain the list of all
registered categories. Bear in mind that each plugin registers its own
categories, so, when installing or removing plugins, this list can
change.

Use `GST_DEBUG` when the error information posted on the GStreamer bus
does not help you nail down a problem. It is common practice to redirect
the output log to a file, and then examine it later, searching for
specific messages.

GStreamer allows for custom debugging information handlers but when
using the default one, the content of each line in the debug output
looks like:

```
0:00:00.868050000  1592   09F62420 WARN                 filesrc gstfilesrc.c:1044:gst_file_src_start:<filesrc0> error: No such file "non-existing-file.webm"
```

And this is how the information formatted:

```
| Example          | Explained                                                 |
|------------------|-----------------------------------------------------------|
|0:00:00.868050000 | Time stamp in HH:MM:SS.sssssssss format since the start of|
|                  | the program.                                              |
|1592              | Process ID from which the message was issued. Useful when |
|                  | your problem involves multiple processes.                 |
|09F62420          | Thread ID from which the message was issued. Useful when  |
|                  | your problem involves multiple threads.                   |
|WARN              | Debug level of the message.                               |
|filesrc           | Debug Category of the message.                            |
|gstfilesrc.c:1044 | Source file and line in the GStreamer source code where   |
|                  | this message was issued.                                  |
|gst_file_src_start| Function that issued the message.                         |
|<filesrc0>        | Name of the object that issued the message. It can be an  |
|                  | element, a pad, or something else. Useful when you have   |
|                  | multiple elements of the same kind and need to distinguish|
|                  | among them. Naming your elements with the name property   |
|                  | makes this debug output more readable but GStreamer       |
|                  | assigns each new element a unique name by default.        |
| error: No such   |                                                           |
| file ....        | The actual message.                                       |
+------------------------------------------------------------------------------+
```

### Adding your own debug information

In the parts of your code that interact with GStreamer, it is
interesting to use GStreamer’s debugging facilities. In this way, you
have all debug output in the same file and the temporal relationship
between different messages is preserved.

To do so, use the `GST_ERROR()`, `GST_WARNING()`, `GST_INFO()`,
`GST_LOG()` and `GST_DEBUG()` macros. They accept the same parameters as
`printf`, and they use the `default` category (`default` will be shown
as the Debug category in the output log).

To change the category to something more meaningful, add these two lines
at the top of your code:

``` c
GST_DEBUG_CATEGORY_STATIC (my_category);
#define GST_CAT_DEFAULT my_category
```

And then this one after you have initialized GStreamer with
`gst_init()`:

``` c
GST_DEBUG_CATEGORY_INIT (my_category, "my category", 0, "This is my very own");
```

This registers a new category (this is, for the duration of your
application: it is not stored in any file), and sets it as the default
category for your code. See the documentation
for `GST_DEBUG_CATEGORY_INIT()`.

### Getting pipeline graphs

For those cases where your pipeline starts to grow too large and you
lose track of what is connected with what, GStreamer has the capability
to output graph files. These are `.dot` files, readable with free
programs like [GraphViz](http://www.graphviz.org), that describe the
topology of your pipeline, along with the caps negotiated in each link.

This is also very handy when using all-in-one elements like `playbin`
 or `uridecodebin`, which instantiate several elements inside them. Use
the `.dot` files to learn what pipeline they have created inside (and
learn a bit of GStreamer along the way).

To obtain `.dot` files, simply set
the `GST_DEBUG_DUMP_DOT_DIR` environment variable to point to the
folder where you want the files to be placed. `gst-launch-1.0` will create
a `.dot` file at each state change, so you can see the evolution of the
caps negotiation. Unset the variable to disable this facility. From
within your application, you can use the
`GST_DEBUG_BIN_TO_DOT_FILE()` and
`GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS()` macros to generate `.dot` files
at your convenience.

Here you have an example of the kind of pipelines that playbin
generates. It is very complex because `playbin` can handle many
different cases: Your manual pipelines normally do not need to be this
long. If your manual pipeline is starting to get very big, consider
using `playbin`.

![](images/playbin.png)

To download the full-size picture, use the attachments link at the top
of this page (It's the paperclip icon).

## Conclusion

This tutorial has shown:

  - How to get more debug information from GStreamer using the
    `GST_DEBUG` environment variable.
  - How to print your own debug information into the GStreamer log with
    the `GST_ERROR()` macro and relatives.
  - How to get pipeline graphs with the
    `GST_DEBUG_DUMP_DOT_DIR` environment variable.

It has been a pleasure having you here, and see you soon!
