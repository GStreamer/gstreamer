//
//
// Copyright (C) 2006 Novell Inc.
// Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
//
// This class implements functions to bind callbacks to GObject signals
// dynamically and to emit signals dynamically.
//
//

using GLib;
using System;
using System.Runtime.InteropServices;
using System.Collections;

namespace Gst {

  public delegate void DynamicSignalHandler (object o, SignalArgs args);

  delegate void GClosureMarshal (IntPtr closure, ref GLib.Value retval, uint argc, IntPtr argsPtr,
                                 IntPtr invocation_hint, IntPtr data);

  public static class DynamicSignal {

    private static readonly int gvalue_struct_size = Marshal.SizeOf (typeof (GLib.Value));

    class ObjectSignalKey {
      object o;
      string signal_name;

      public ObjectSignalKey (object o, string name) {
        this.o = o;
        signal_name = name;
      }

      public override bool Equals (object o) {
        if (o is ObjectSignalKey) {
          ObjectSignalKey k = (ObjectSignalKey) o;
          return k.o.Equals (this.o) && signal_name.Equals (k.signal_name);
        }
        return base.Equals (o);
      }

      public override int GetHashCode() {
        return o.GetHashCode() ^ signal_name.GetHashCode();
      }
    }

    class SignalInfo {
      uint handlerId;
      IntPtr closure;
      Delegate registeredHandler;

      public IntPtr Closure {
        get {
          return closure;
        }
        set {
          closure = value;
        }
      }

      public uint HandlerId {
        get {
          return handlerId;
        }
        set {
          handlerId = value;
        }
      }

      public Delegate RegisteredHandler {
        get {
          return registeredHandler;
        }
        set {
          registeredHandler = value;
        }
      }

      public SignalInfo (uint handlerId, IntPtr closure, Delegate registeredHandler) {
        this.handlerId = handlerId;
        this.closure = closure;
        this.registeredHandler = registeredHandler;
      }
    }

    static Hashtable SignalHandlers = new Hashtable();

    static GClosureMarshal marshalHandler = new GClosureMarshal (OnMarshal);

    public static void Connect (GLib.Object o, string name, DynamicSignalHandler handler) {
      Connect (o, name, false, handler);
    }

    static int g_closure_sizeof = gstsharp_g_closure_sizeof ();

    public static void Connect (GLib.Object o, string name,
                                bool after, DynamicSignalHandler handler) {
      Delegate newHandler;

      ObjectSignalKey k = new ObjectSignalKey (o, name);

      if (SignalHandlers[k] != null) {
        SignalInfo si = (SignalInfo) SignalHandlers[k];
        newHandler = Delegate.Combine (si.RegisteredHandler, handler);
        si.RegisteredHandler = newHandler;
      } else {
        IntPtr closure = g_closure_new_simple (g_closure_sizeof, IntPtr.Zero);
        g_closure_set_meta_marshal (closure, (IntPtr) GCHandle.Alloc (k),  marshalHandler);
        uint signalId = g_signal_connect_closure (o.Handle, name, closure, after);
        SignalHandlers.Add (k, new SignalInfo (signalId, closure, handler));
      }
    }

    [DllImport ("gstreamersharpglue-0.10") ]
    static extern int gstsharp_g_closure_sizeof ();

    public static void Disconnect (GLib.Object o, string name, DynamicSignalHandler handler) {
      ObjectSignalKey k = new ObjectSignalKey (o, name);
      if (SignalHandlers[k] != null) {
        SignalInfo si = (SignalInfo) SignalHandlers[k];
        Delegate newHandler = Delegate.Remove (si.RegisteredHandler, handler);
        if (newHandler == null || handler == null) {
          g_signal_handler_disconnect (o.Handle, si.HandlerId);
          SignalHandlers.Remove (k);
        } else {
          si.RegisteredHandler = newHandler;
        }
      }
    }

    static void OnMarshal (IntPtr closure, ref GLib.Value retval, uint argc, IntPtr argsPtr,
                           IntPtr ihint, IntPtr data) {
      object [] args = new object[argc - 1];
      object o = ( (GLib.Value) Marshal.PtrToStructure (argsPtr, typeof (GLib.Value))).Val;

      for (int i = 1; i < argc; i++) {
        IntPtr struct_ptr = (IntPtr) ( (long) argsPtr + (i * gvalue_struct_size));
        GLib.Value argument = (GLib.Value) Marshal.PtrToStructure (struct_ptr, typeof (GLib.Value));
        args[i - 1] = argument.Val;
      }

      if (data == IntPtr.Zero) {
        Console.Error.WriteLine ("No available data");
      }

      ObjectSignalKey k = (ObjectSignalKey) ( (GCHandle) data).Target;
      if (k != null) {
        SignalArgs arg = new SignalArgs();
        arg.Args = args;
        SignalInfo si = (SignalInfo) SignalHandlers[k];
        DynamicSignalHandler handler = (DynamicSignalHandler) si.RegisteredHandler;
        handler (o, arg);
        if (arg.RetVal != null) {
          retval.Val = arg.RetVal;
        }
      }
    }


    [DllImport ("gobject-2.0.dll") ]
    static extern IntPtr g_closure_new_simple (int size, IntPtr data);

    [DllImport ("gobject-2.0.dll") ]
    static extern uint g_signal_connect_closure (IntPtr instance,
          string name, IntPtr closure, bool after);

    [DllImport ("gobject-2.0.dll") ]
    static extern void g_closure_set_meta_marshal (IntPtr closure, IntPtr data, GClosureMarshal marshal);

    class GTypeSignalKey {
      GType type;
      string signal_name;

      public GTypeSignalKey (GType type, string name) {
        this.type = type;
        signal_name = name;
      }

      public override bool Equals (object o) {
        if (o is GTypeSignalKey) {
          GTypeSignalKey k = (GTypeSignalKey) o;
          return k.type.Equals (this.type) && signal_name.Equals (k.signal_name);
        }
        return base.Equals (o);
      }

      public override int GetHashCode() {
        return type.GetHashCode() ^ signal_name.GetHashCode();
      }
    }

    struct SignalQuery {
      public uint signal_id;
      public string signal_name;
      public GType itype;
      public uint signal_flags;
      public GType return_type;
      public uint n_params;
      public Type[] param_types;
    }

    static Hashtable SignalEmitInfo = new Hashtable ();

    public static object Emit (GLib.Object o, string name, params object[] parameters) {
      SignalQuery query;
      IntPtr type = gstsharp_g_type_from_instance (o.Handle);
      GType gtype = new GType (type);
      string signal_name, signal_detail;
      uint signal_detail_quark = 0;
      int colon;

      colon = name.LastIndexOf ("::");

      if (colon == -1) {
        signal_name = name;
        signal_detail = String.Empty;
      } else {
        signal_name = name.Substring (0, colon);
        signal_detail = name.Substring (colon + 2);
      }

      GTypeSignalKey key = new GTypeSignalKey (gtype, signal_name);

      if (SignalEmitInfo[key] == null) {
        uint signal_id = g_signal_lookup (signal_name, type);

        if (signal_id == 0)
          throw new NotSupportedException (String.Format ("{0} has no signal of name {1}", o, name));
        GSignalQuery q = new GSignalQuery ();
        g_signal_query (signal_id, ref q);

        if (q.signal_id == 0)
          throw new NotSupportedException (String.Format ("{0} couldn't be queried for signal with name {1}", o, name));

        query = new SignalQuery ();

        query.signal_id = signal_id;
        query.signal_name = Marshaller.Utf8PtrToString (q.signal_name);
        query.itype = new GType (q.itype);
        query.signal_flags = q.signal_flags;
        query.return_type = new GType (q.return_type);
        query.n_params = q.n_params;
        query.param_types = new Type[q.n_params];

        for (int i = 0; i < query.n_params; i++) {
          IntPtr t = Marshal.ReadIntPtr (q.param_types, i);
          GType g = new GType (t);

          query.param_types[i] = (Type) g;
        }

        SignalEmitInfo.Add (key, query);
      }

      query = (SignalQuery) SignalEmitInfo[key];
      GLib.Value[] signal_parameters = new GLib.Value[query.n_params + 1];
      signal_parameters[0] = new GLib.Value (o);

      if (parameters.Length != query.n_params)
        throw new ApplicationException (String.Format ("Invalid number of parameters: expected {0}, got {1}", query.n_params, parameters.Length));

      for (int i = 0; i < query.n_params; i++) {
        Type expected_type = (Type) query.param_types[i];
        Type given_type = parameters[i].GetType ();

        if (expected_type != given_type && ! given_type.IsSubclassOf (given_type))
          throw new ApplicationException (String.Format ("Invalid parameter type: expected {0}, got {1}", expected_type, given_type));

        signal_parameters[i + 1] = new GLib.Value (parameters[i]);
      }

      GLib.Value return_value = new GLib.Value ();
      if (query.return_type != GType.Invalid && query.return_type != GType.None)
        return_value.Init (query.return_type);

      if (signal_detail != String.Empty)
        signal_detail_quark = g_quark_from_string (signal_detail);

      g_signal_emitv (signal_parameters, query.signal_id, signal_detail_quark, ref return_value);

      return (query.return_type != GType.Invalid && query.return_type != GType.None) ? return_value.Val : null;
    }

    [DllImport ("gstreamersharpglue-0.10") ]
    static extern IntPtr gstsharp_g_type_from_instance (IntPtr o);

    [DllImport ("gobject-2.0.dll") ]
    static extern int g_signal_handler_disconnect (IntPtr o, uint handler_id);

    [DllImport ("gobject-2.0.dll") ]
    static extern uint g_signal_lookup (string name, IntPtr itype);

    [DllImport ("glib-2.0.dll") ]
    static extern uint g_quark_from_string (string str);

    [DllImport ("gobject-2.0.dll") ]
    static extern void g_signal_emitv (GLib.Value[] parameters, uint signal_id, uint detail, ref GLib.Value return_value);

    [StructLayout (LayoutKind.Sequential) ]
    struct GSignalQuery {
      public uint signal_id;
      public IntPtr signal_name;
      public IntPtr itype;
      public uint signal_flags;
      public IntPtr return_type;
      public uint n_params;
      public IntPtr param_types;
    }

    [DllImport ("gobject-2.0.dll") ]
    static extern void g_signal_query (uint signal_id, ref GSignalQuery query);
  }
}
