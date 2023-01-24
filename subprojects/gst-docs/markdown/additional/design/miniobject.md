# GstMiniObject

This document describes the design of the `GstMiniObject` base class.

The `GstMiniObject` abstract base class is used to construct lightweight,
refcounted and boxed types that are frequently created and destroyed.

## Requirements

- Be lightweight
- Refcounted
- I must be possible to control access to the object, ie. when the
object is readable and writable.
- Subclasses must be able to use their own allocator for the memory.

## Usage

Users of the `GstMiniObject` infrastructure will need to define a
structure that includes the `GstMiniObject` structure as the first field.

``` c
struct {
  GstMiniObject mini_object;

  /* my fields */
  ...
} MyObject
```

The subclass should then implement a constructor method where it
allocates the memory for its structure and initializes the miniobject
structure with `gst_mini_object_init()`. Copy and Free functions are
provided to the `gst_mini_object_init()` function.

``` c
MyObject *
my_object_new()
{
  MyObject *res = g_new (MyObject, 1);

  gst_mini_object_init (GST_MINI_OBJECT_CAST (res), 0,
        MY_TYPE_OBJECT,
        (GstMiniObjectCopyFunction) _my_object_copy,
        (GstMiniObjectDisposeFunction) NULL,
        (GstMiniObjectFreeFunction) _my_object_free);

    /* other init */
    .....

  return res;
}
```

The Free function is responsible for freeing the allocated memory for
the structure.

``` c
static void
_my_object_free (MyObject *obj)
{
  /* other cleanup */
  ...

  g_free (obj);
}
```

## Lifecycle

`GstMiniObject` is refcounted. When a `GstMiniObject` is first created, it
has a refcount of 1.

Each variable holding a reference to a `GstMiniObject` is responsible for
updating the refcount. This includes incrementing the refcount with
`gst_mini_object_ref()` when a reference is kept to a miniobject or
`gst_mini_object_unref()` when a reference is released.

When the refcount reaches 0, and thus no objects hold a reference to the
miniobject anymore, we can free the miniobject.

When freeing the miniobject, first the `GstMiniObjectDisposeFunction` is
called. This function is allowed to revive the object again by
incrementing the refcount, in which case it should return FALSE from the
dispose function. The dispose function is used by `GstBuffer` to revive
the buffer back into the `GstBufferPool` when needed.

When the dispose function returns TRUE, the `GstMiniObjectFreeFunction`
will be called and the miniobject will be freed.

## Copy

A miniobject can be copied with `gst_mini_object_copy()`. This function
will call the custom copy function that was provided when registering
the new `GstMiniObject` subclass.

The copy function should try to preserve as much info from the original
object as possible.

The new copy should be writable.

## Access management

`GstMiniObject` can be shared between multiple threads. It is important
that when a thread writes to a `GstMiniObject` that the other threads
don’t not see the changes.

To avoid exposing changes from one thread to another thread, the
miniobjects are managed in a Copy-On-Write way. A copy is only made when
it is known that the object is shared between multiple objects or
threads.

There are 2 methods implemented for controlling access to the miniobject.

  - A first method relies on the refcount of the object to control
    writability. Objects using this method have the `LOCKABLE` flag unset.

  - A second method relies on a separate counter for controlling the
    access to the object. Objects using this method have the LOCKABLE
    flag set.
    You can check if an object is writable with `gst_mini_object_is_writable()` and
    you can make any miniobject writable with `gst_mini_object_make_writable()`.
    This will create a writable copy when the object was not writable.

### non-LOCKABLE GstMiniObjects

These `GstMiniObjects` have the `LOCKABLE` flag unset. They use the refcount
value to control writability of the object.

When the refcount of the miniobject is > 1, the objects it referenced by at
least 2 objects and is thus considered unwritable. A copy must be made before a
modification to the object can be done.

Using the refcount to control writability is problematic for many language
bindings that can keep additional references to the objects. This method is
mainly for historical reasons until all users of the miniobjects are
converted to use the `LOCKABLE` flag.

### LOCKABLE GstMiniObjects

These `GstMiniObjects` have the `LOCKABLE` flag set. They use a separate counter
for controlling writability and access to the object.

It consists of 2 components:

#### exclusive counter

Each object that wants to keep a reference to a `GstMiniObject` and doesn't
want to see the changes from other owners of the same `GstMiniObject` needs to
lock the `GstMiniObject` in `EXCLUSIVE` mode, which will increase the exclusive
counter.

The exclusive counter counts the amount of objects that share this
`GstMiniObject`. The counter is initially 0, meaning that the object is not
shared with any object.

When a reference to a `GstMiniObject` release, both the ref count and the
exclusive counter will be decreased with `gst_mini_object_unref()` and
`gst_mini_object_unlock()` respectively.

#### locking

All read and write access must be performed between a `gst_mini_object_lock()`
and `gst_mini_object_unlock()` pair with the requested access method.

A `gst_mini_object_lock()` can fail when a `WRITE` lock is requested and the
exclusive counter is > 1. Indeed a `GstMiniObject` object with an exclusive
counter > 1 is locked `EXCLUSIVELY` by at least 2 objects and is therefore not
writable.

Once the `GstMiniObject` is locked with a certain access mode, it can be
recursively locked with the same or narrower access mode. For example, first
locking the `GstMiniObject` in `READWRITE` mode allows you to recusively lock
the `GstMiniObject` in `READWRITE`, `READ` and `WRITE` mode. Memory locked in
`READ` mode cannot be locked recursively in `WRITE` or `READWRITE` mode.

Note that multiple threads can `READ`-lock the `GstMiniObject` concurrently but
cannot lock the object in `WRITE` mode because the exclusive counter must
be > 1.

All calls to `gst_mini_object_lock()` need to be paired with one
`gst_mini_object_unlock()` call with the same access mode. When the last
refcount of the object is removed, there should be no more outstanding locks.

Note that a shared counter of both 0 and 1 leaves the `GstMiniObject` writable.
The reason is to make it easy to create and pass ownership of the
`GstMiniObject` to another object while keeping it writable. When the
`GstMiniObject` is created with a shared count of 0, it is writable. When the
`GstMiniObject` is then added to another object, the shared count is incremented
to 1 and the `GstMiniObject` remains writable. The 0 share counter has a similar
purpose as the floating reference in `GObject`.

## Weak references

`GstMiniObject` has support for weak references. A callback will be called
when the object is freed for all registered weak references.

## QData

Extra data can be associated with a `GstMiniObject` by using the `QData`
API.
