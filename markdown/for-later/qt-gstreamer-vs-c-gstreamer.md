# QtGStreamer vs C GStreamer

QtGStreamer is designed to mirror the C GStreamer API as closely as
possible. There are, of course, minor differences. They are documented
here.

## Common Functions

<table>
<colgroup>
<col width="50%" />
<col width="50%" />
</colgroup>
<thead>
<tr class="header">
<th>C GStreamer</th>
<th>QtGStreamer</th>
</tr>
</thead>
<tbody>
<tr class="odd">
<td><code>gst_element_factory_make()</code></td>
<td><code>QGst::ElementFactory::make(const QString &amp;factoryName, const char *elementName=NULL)</code></td>
</tr>
<tr class="even">
<td><code>gst_parse_bin_from_description()</code></td>
<td><code>QGst::Bin::fromDescription(const QString &amp;description, BinFromDescriptionOption ghostUnlinkedPads=Ghost)</code></td>
</tr>
<tr class="odd">
<td><code>gst_caps_from_string()</code></td>
<td><p><code>QGst::Caps::fromString(const QString &amp;string)</code></p></td>
</tr>
<tr class="even">
<td><code>g_signal_connect()</code></td>
<td><code>QGlib::connect(GObject* instance, const char *detailedSignal, T *receiver, R(T::*)(Args...) slot, ConnectFlags flags)</code></td>
</tr>
</tbody>
</table>

## Naming Convention

QtGStreamer follows a strict naming policy to help make cross
referencing easier:

#### Namespaces

The "G" namespace (`GObject`, `GValue`, etc...) is referred to as
"QGlib".

The "Gst" namespace (`GstObject`, `GstElement`, etc...) is referred to
as "QGst".

#### Class Names

Class names should be the same as their G\* equivalents, with the
namespace prefix removed. For example, "`GstObject`" becomes
"`QGst::Object`", "`GParamSpec`" becomes "`QGlib::ParamSpec`", etc...

#### Method Names

In general the method names should be the same as the GStreamer ones,
with the g\[st\]\_\<class\> prefix removed and converted to camel case.

For example,

``` c
gboolean gst_caps_is_emtpy(const GstCaps *caps);
```

becomes:

``` c
namespace QGst {
    class Caps {
        bool isEmpty() const;
    }
}
```

There are cases where this may not be followed:

1.  **Properties**. Most property getters have a "get" prefix, for
    example, `gst_object_get_name()`. In QtGStreamer the "get" prefix is
    omitted, so this becomes just `name()`.
2.  **Overloaded members**. In C there is no possibility to have two
    methods with the same name, so overloaded members usually have some
    extra suffix, like "\_full". For example, `g_object_set_data()` and
    `g_object_set_data_full()`. In C++ we just add a method with the
    same name, or put optional parameters in the existing method.
3.  **Other cases where the glib/gstreamer method name doesn't make much
    sense**. For example, `gst_element_is_locked_state()`. That doesn't
    make sense in english, as "sate" is the subject and should go before
    the verb "is". So, it becomes `stateIsLocked()`.

## Reference Counting

Reference counting is handled the same way as Qt does. There is no need
to call `g_object_ref()`` and g_object_unref()`.

## Access to GStreamer Elements

QtGStreamer provides access to the underlying C objects, in case you
need them. This is accessible with a simple cast:

``` c
ElementPtr qgstElement = QGst::ElementFactory::make("playbin");
GstElement* gstElement = GST_ELEMENT(qgstElement);
```
