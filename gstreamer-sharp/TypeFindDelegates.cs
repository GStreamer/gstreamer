using System;
using System.Runtime.InteropServices;
using System.Collections;

namespace Gst {
  public delegate byte[] TypeFindPeekFunction (long offset, uint size);
  public delegate void TypeFindSuggestFunction (uint propability, Gst.Caps caps);
  public delegate ulong TypeFindGetLengthFunction ();
}

namespace GstSharp {

  [Gst.GLib.CDeclCallback]
  internal delegate IntPtr TypeFindPeekFunctionNative (IntPtr data, long offset, uint size);

  internal class TypeFindPeekFunctionInvoker {

    TypeFindPeekFunctionNative native_cb;
    IntPtr __data;
    Gst.GLib.DestroyNotify __notify;

    ~TypeFindPeekFunctionInvoker () {
      if (__notify == null)
        return;
      __notify (__data);
    }

    internal TypeFindPeekFunctionInvoker (TypeFindPeekFunctionNative native_cb) : this (native_cb, IntPtr.Zero, null) {}

    internal TypeFindPeekFunctionInvoker (TypeFindPeekFunctionNative native_cb, IntPtr data) : this (native_cb, data, null) {}

    internal TypeFindPeekFunctionInvoker (TypeFindPeekFunctionNative native_cb, IntPtr data, Gst.GLib.DestroyNotify notify) {
      this.native_cb = native_cb;
      __data = data;
      __notify = notify;
    }

    internal Gst.TypeFindPeekFunction Handler {
      get {
        return new Gst.TypeFindPeekFunction (InvokeNative);
      }
    }

    byte[] InvokeNative (long offset, uint size) {
      IntPtr raw_ret = native_cb (IntPtr.Zero, offset, size);
      if (raw_ret == IntPtr.Zero)
        return new byte[] {};

      byte[] ret = new byte[size];
      Marshal.Copy (raw_ret, ret, 0, (int) size);

      return ret;
    }
  }

  internal class TypeFindPeekFunctionWrapper {
    /* FIXME: We should do something more intelligent here */
    private ArrayList data_cache = new ArrayList ();

    public IntPtr NativeCallback (IntPtr data, long offset, uint size) {
      try {
        byte[] __ret = managed (offset, size);
        if (release_on_call)
          gch.Free ();
        if (__ret.Length == 0)
          return IntPtr.Zero;

        IntPtr raw_ret = Marshal.AllocHGlobal (__ret.Length);
        Marshal.Copy (__ret, 0, raw_ret, (int) size);
        data_cache.Add (raw_ret);
        return raw_ret;
      } catch (Exception e) {
        Gst.GLib.ExceptionManager.RaiseUnhandledException (e, true);
        // NOTREACHED: Above call does not return.
        throw e;
      }
    }

    ~TypeFindPeekFunctionWrapper() {
      foreach (IntPtr raw in data_cache)
        Marshal.FreeHGlobal (raw);
      data_cache = null;
    }

    bool release_on_call = false;
    GCHandle gch;

    public void PersistUntilCalled () {
      release_on_call = true;
      gch = GCHandle.Alloc (this);
    }

    internal TypeFindPeekFunctionNative NativeDelegate;
    Gst.TypeFindPeekFunction managed;

    public TypeFindPeekFunctionWrapper (Gst.TypeFindPeekFunction managed) {
      this.managed = managed;
      if (managed != null)
        NativeDelegate = new TypeFindPeekFunctionNative (NativeCallback);
    }

    public static Gst.TypeFindPeekFunction GetManagedDelegate (TypeFindPeekFunctionNative native) {
      if (native == null)
        return null;
      TypeFindPeekFunctionWrapper wrapper = (TypeFindPeekFunctionWrapper) native.Target;
      if (wrapper == null)
        return null;
      return wrapper.managed;
    }
  }

  [Gst.GLib.CDeclCallback]
  internal delegate void TypeFindSuggestFunctionNative (IntPtr data, uint propability, IntPtr caps);

  internal class TypeFindSuggestFunctionInvoker {

    TypeFindSuggestFunctionNative native_cb;
    IntPtr __data;
    Gst.GLib.DestroyNotify __notify;

    ~TypeFindSuggestFunctionInvoker () {
      if (__notify == null)
        return;
      __notify (__data);
    }

    internal TypeFindSuggestFunctionInvoker (TypeFindSuggestFunctionNative native_cb) : this (native_cb, IntPtr.Zero, null) {}

    internal TypeFindSuggestFunctionInvoker (TypeFindSuggestFunctionNative native_cb, IntPtr data) : this (native_cb, data, null) {}

    internal TypeFindSuggestFunctionInvoker (TypeFindSuggestFunctionNative native_cb, IntPtr data, Gst.GLib.DestroyNotify notify) {
      this.native_cb = native_cb;
      __data = data;
      __notify = notify;
    }

    internal Gst.TypeFindSuggestFunction Handler {
      get {
        return new Gst.TypeFindSuggestFunction (InvokeNative);
      }
    }

    void InvokeNative (uint propability, Gst.Caps caps) {
      native_cb (IntPtr.Zero, propability, caps.Handle);
    }
  }

  internal class TypeFindSuggestFunctionWrapper {
    public void NativeCallback (IntPtr data, uint propability, IntPtr caps) {
      try {
        managed (propability, (Gst.Caps) Gst.GLib.Opaque.GetOpaque (caps, typeof (Gst.Caps), false));
        if (release_on_call)
          gch.Free ();
      } catch (Exception e) {
        Gst.GLib.ExceptionManager.RaiseUnhandledException (e, true);
        // NOTREACHED: Above call does not return.
        throw e;
      }
    }

    bool release_on_call = false;
    GCHandle gch;

    public void PersistUntilCalled () {
      release_on_call = true;
      gch = GCHandle.Alloc (this);
    }

    internal TypeFindSuggestFunctionNative NativeDelegate;
    Gst.TypeFindSuggestFunction managed;

    public TypeFindSuggestFunctionWrapper (Gst.TypeFindSuggestFunction managed) {
      this.managed = managed;
      if (managed != null)
        NativeDelegate = new TypeFindSuggestFunctionNative (NativeCallback);
    }

    public static Gst.TypeFindSuggestFunction GetManagedDelegate (TypeFindSuggestFunctionNative native) {
      if (native == null)
        return null;
      TypeFindSuggestFunctionWrapper wrapper = (TypeFindSuggestFunctionWrapper) native.Target;
      if (wrapper == null)
        return null;
      return wrapper.managed;
    }
  }

  [Gst.GLib.CDeclCallback]
  internal delegate ulong TypeFindGetLengthFunctionNative (IntPtr data);

  internal class TypeFindGetLengthFunctionInvoker {

    TypeFindGetLengthFunctionNative native_cb;
    IntPtr __data;
    Gst.GLib.DestroyNotify __notify;

    ~TypeFindGetLengthFunctionInvoker () {
      if (__notify == null)
        return;
      __notify (__data);
    }

    internal TypeFindGetLengthFunctionInvoker (TypeFindGetLengthFunctionNative native_cb) : this (native_cb, IntPtr.Zero, null) {}

    internal TypeFindGetLengthFunctionInvoker (TypeFindGetLengthFunctionNative native_cb, IntPtr data) : this (native_cb, data, null) {}

    internal TypeFindGetLengthFunctionInvoker (TypeFindGetLengthFunctionNative native_cb, IntPtr data, Gst.GLib.DestroyNotify notify) {
      this.native_cb = native_cb;
      __data = data;
      __notify = notify;
    }

    internal Gst.TypeFindGetLengthFunction Handler {
      get {
        return new Gst.TypeFindGetLengthFunction (InvokeNative);
      }
    }

    ulong InvokeNative () {
      ulong ret = native_cb (IntPtr.Zero);
      return ret;
    }
  }

  internal class TypeFindGetLengthFunctionWrapper {
    public ulong NativeCallback (IntPtr data) {
      try {
        ulong ret = managed ();
        if (release_on_call)
          gch.Free ();
        return ret;
      } catch (Exception e) {
        Gst.GLib.ExceptionManager.RaiseUnhandledException (e, true);
        // NOTREACHED: Above call does not return.
        throw e;
      }
    }

    bool release_on_call = false;
    GCHandle gch;

    public void PersistUntilCalled () {
      release_on_call = true;
      gch = GCHandle.Alloc (this);
    }

    internal TypeFindGetLengthFunctionNative NativeDelegate;
    Gst.TypeFindGetLengthFunction managed;

    public TypeFindGetLengthFunctionWrapper (Gst.TypeFindGetLengthFunction managed) {
      this.managed = managed;
      if (managed != null)
        NativeDelegate = new TypeFindGetLengthFunctionNative (NativeCallback);
    }

    public static Gst.TypeFindGetLengthFunction GetManagedDelegate (TypeFindGetLengthFunctionNative native) {
      if (native == null)
        return null;
      TypeFindGetLengthFunctionWrapper wrapper = (TypeFindGetLengthFunctionWrapper) native.Target;
      if (wrapper == null)
        return null;
      return wrapper.managed;
    }
  }
}
