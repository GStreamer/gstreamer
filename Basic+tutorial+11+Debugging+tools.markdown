# Basic tutorial 11: Debugging tools

This page last changed on Jun 04, 2012 by xartigas.

# Goal

Sometimes things won’t go as expected and the error messages retrieved
from the bus (if any) just don’t provide enough information. Luckily,
GStreamer ships with massive amounts of debug information, which usually
hint what the problem might be. This tutorial shows:

  - How to get more debug information from GStreamer.

  - How to print your own debug information into the GStreamer log.

  - How to get pipeline graphs

# Printing debug information

### The debug log

GStreamer and its plugins are full of debug traces, this is, places in
the code where a particularly interesting piece of information is
printed to the console, along with time stamping, process, category,
source code file, function and element information.

The debug output is controlled with the `GST_DEBUG` environment
variable. Here’s an example with
`GST_DEBUG=2`:

``` theme: Default; brush: plain; gutter: false
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

<table>
<tbody>
<tr class="odd">
<td><span style="color: rgb(192,192,192);">0</span></td>
</tr>
<tr class="even">
<td><span style="color: rgb(192,192,192);">1</span></td>
</tr>
<tr class="odd">
<td><span style="color: rgb(192,192,192);">2</span></td>
</tr>
<tr class="even">
<td><span style="color: rgb(192,192,192);">3</span></td>
</tr>
<tr class="odd">
<td><span style="color: rgb(192,192,192);">4</span></td>
</tr>
<tr class="even">
<td><span style="color: rgb(192,192,192);">5</span></td>
</tr>
</tbody>
</table>

To enable debug output, set the `GST_DEBUG` environment variable to the
desired debug level. All levels below that will also be shown (i.e., if
you set `GST_DEBUG=2`, you will get both `ERROR` and
`WARNING` messages).

Furthermore, each plugin or part of the GStreamer defines its own
category, so you can specify a debug level for each individual category.
For example, `GST_DEBUG=2,audiotestsrc:5`, will use Debug Level 5 for
the `audiotestsrc` element, and 2 for all the others.

The `GST_DEBUG` environment variable, then, is a comma-separated list of
*category*:*level* pairs, with an optional *level* at the beginning,
representing the default debug level for all categories.

The `'*'` wildcard is also available. For example
`GST_DEBUG=2,audio*:5` will use Debug Level 5 for all categories
starting with the word `audio`. `GST_DEBUG=*:2` is equivalent to
`GST_DEBUG=2`.

Use `gst-launch-0.10 --gst-debug-help` to obtain the list of all
registered categories. Bear in mind that each plugin registers its own
categories, so, when installing or removing plugins, this list can
change.

Use `GST_DEBUG` when the error information posted on the GStreamer bus
does not help you nail down a problem. It is common practice to redirect
the output log to a file, and then examine it later, searching for
specific messages.

The content of each line in the debug output
is:

``` theme: Default; brush: plain; gutter: false
0:00:00.868050000  1592   09F62420 WARN                 filesrc gstfilesrc.c:1044:gst_file_src_start:<filesrc0> error: No such file "non-existing-file.webm"
```

<table>
<thead>
<tr class="header">
<th><code>0:00:00.868050000</code></th>
<th>Time stamp in HH:MM:SS.sssssssss format since the start of the program</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><code>1592</code></td>
<td>Process ID from which the message was issued. Useful when your problem involves multiple processes</td>
</tr>
<tr class="even">
<td><code>09F62420</code></td>
<td><span>Thread ID from which the message was issued. Useful when your problem involves multiple threads</span></td>
</tr>
<tr class="odd">
<td><code>WARN</code></td>
<td>Debug level of the message</td>
</tr>
<tr class="even">
<td><code>filesrc</code></td>
<td>Debug Category of the message</td>
</tr>
<tr class="odd">
<td><code>gstfilesrc.c:1044</code></td>
<td>Source file and line in the GStreamer source code where this message is printed</td>
</tr>
<tr class="even">
<td><code>gst_file_src_start</code></td>
<td>Function from which the message was issued</td>
</tr>
<tr class="odd">
<td><code>&lt;filesrc0&gt;</code></td>
<td>Name of the object that issued the message. It can be an element, a Pad, or something else. Useful when you have multiple elements of the same kind and need to distinguish among them. Naming your elements with the name property will make this debug output more readable (otherwise, GStreamer assigns each new element a unique name).</td>
</tr>
<tr class="even">
<td><code>error: No such file &quot;non-existing-file.webm&quot;</code></td>
<td>The actual message.</td>
</tr>
</tbody>
</table>

### Adding your own debug information

In the parts of your code that interact with GStreamer, it is
interesting to use GStreamer’s debugging facilities. In this way, you
have all debug output in the same file and the temporal relationship
between different messages is preserved.

To do so, use the `GST_ERROR()`, `GST_WARNING()`, `GST_INFO()`,
`GST_LOG()` and `GST_DEBUG()` macros. They accept the same parameters as
`printf`, and they use the `default` category (`default` will be shown
as the Debug category in the output log).

To change the category to something more meaningful, add these two lines
at the top of your code:

``` theme: Default; brush: cpp; gutter: true
GST_DEBUG_CATEGORY_STATIC (my_category);
#define GST_CAT_DEFAULT my_category
```

And then this one after you have initialized GStreamer with
`gst_init()`:

``` theme: Default; brush: cpp; gutter: false
GST_DEBUG_CATEGORY_INIT (my_category, "my category", 0, "This is my very own");
```

This registers a new category (this is, for the duration of your
application: it is not stored in any file), and sets it as the default
category for your code. See the documentation
for `GST_DEBUG_CATEGORY_INIT()`.

### Getting pipeline graphs

For those cases where your pipeline starts to grow too large and you
lose track of what is connected with what, GStreamer has the capability
to output graph files. These are `.dot` files, readable with free
programs like [GraphViz](http://www.graphviz.org), that describe the
topology of your pipeline, along with the caps negotiated in each link.

This is also very handy when using all-in-one elements like `playbin2`
 or `uridecodebin`, which instantiate several elements inside them. Use
the `.dot` files to learn what pipeline they have created inside (and
learn a bit of GStreamer along the way).

To obtain `.dot` files, simply set
the `GST_DEBUG_DUMP_DOT_DIR` environment variable to point to the
folder where you want the files to be placed. `gst-launch` will create
a `.dot` file at each state change, so you can see the evolution of the
caps negotiation. Unset the variable to disable this facility. From
within your application, you can use the
`GST_DEBUG_BIN_TO_DOT_FILE()` and
`GST_DEBUG_BIN_TO_DOT_FILE_WITH_TS()` macros to generate `.dot` files
at your convenience.

Here you have an example of the kind of pipelines that playbin2
generates. It is very complex because `playbin2` can handle many
different cases: Your manual pipelines normally do not need to be this
long. If your manual pipeline is starting to get very big, consider
using `playbin2`.

![](attachments/327830/2424840.png)

To download the full-size picture, use the attachments link at the top
of this page (It's the paperclip icon).

# Conclusion

This tutorial has shown:

  - How to get more debug information from GStreamer using the
    `GST_DEBUG` environment variable.
  - How to print your own debug information into the GStreamer log with
    the `GST_ERROR()` macro and relatives.
  - How to get pipeline graphs with the
    `GST_DEBUG_DUMP_DOT_DIR` environment variable.

It has been a pleasure having you here, and see you soon\!

## Attachments:

![](images/icons/bullet_blue.gif)
[playbin2.png](attachments/327830/2424840.png) (image/png)

Document generated by Confluence on Oct 08, 2015 10:27
