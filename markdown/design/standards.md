# Ownership of dynamic objects

Any object-oriented system or language that doesn’t have automatic
garbage collection has many potential pitfalls as far as pointers
go. Therefore, some standards must be adhered to as far as who owns
what.

## Strings

Arguments passed into a function are owned by the caller, and the
function will make a copy of the string for its own internal use. The
string should be `const gchar *`. Strings returned from a function are
always a copy of the original and should be freed after usage by the
caller.

ex:

``` c
name = gst_element_get_name (element);   /* copy of name is made */
.. use name ..
g_free (name);                           /* free after usage */
```

## Objects

Objects passed into a function are owned by the caller, any additional
reference held to the object after leaving the function should increase
its refcount.

Objects returned from a function are owned by the caller. This means
that the caller should `_free()` or `_unref()` the objects after usage.

ex:

``` c
peer = gst_pad_get_peer (pad);          /* peer with increased refcount */
if (peer) {
  .. use peer ..
  gst_object_unref (GST_OBJECT (peer)); /* unref peer after usage */
}
```

## Iterators

When retrieving multiple objects from an object, an iterator should be
used. The iterator allows you to access the objects one after another
while making sure that the retrieved set of objects remains consistent.

Each object retrieved from an iterator has its refcount increased or is
a copy of the original. In any case the object should be unreffed or
freed after usage.
