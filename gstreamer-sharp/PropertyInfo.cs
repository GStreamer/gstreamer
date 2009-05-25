using System;
using System.Runtime.InteropServices;
using GLib;

namespace Gst {
  public struct PropertyInfo {
    internal string name;
    public string Name {
      get {
        return name;
      }
    }

    internal string nick;
    public string Nick {
      get {
        return nick;
      }
    }

    internal string blurb;
    public string Blurb {
      get {
        return blurb;
      }
    }

    internal bool readable;
    public bool Readable {
      get {
        return readable;
      }
    }

    internal bool writeable;
    public bool Writeable {
      get {
        return writeable;
      }
    }

    internal bool controllable;
    public bool Controllable {
      get {
        return controllable;
      }
    }

    internal System.Type type;
    public System.Type Type {
      get {
        return type;
      }
    }

    internal GLib.GType gtype;
    public GLib.GType GType {
      get {
        return gtype;
      }
    }

    internal object dflt;
    public object Default {
      get {
        return dflt;
      }
    }

    internal object min;
    public object Min {
      get {
        return min;
      }
    }

    internal object max;
    public object Max {
      get {
        return max;
      }
    }

    [DllImport ("libgobject-2.0-0.dll") ]
    static extern IntPtr g_param_spec_get_name (IntPtr pspec);

    [DllImport ("libgobject-2.0-0.dll") ]
    static extern IntPtr g_param_spec_get_nick (IntPtr pspec);

    [DllImport ("libgobject-2.0-0.dll") ]
    static extern IntPtr g_param_spec_get_blurb (IntPtr pspec);

    [DllImport ("libgobject-2.0-0.dll") ]
    static extern void g_param_value_set_default (IntPtr pspec, ref GLib.Value val);

    [DllImport ("gstreamersharpglue-0.10.dll") ]
    static extern bool gstsharp_g_param_spec_get_range (IntPtr pspec, ref GLib.Value min, ref GLib.Value max);


    [StructLayout (LayoutKind.Sequential) ]
    struct GTypeInstance {
      public IntPtr g_class;
    }

    [StructLayout (LayoutKind.Sequential) ]
    struct GParamSpec {
      public GTypeInstance instance;
      IntPtr name;
      public int Flags;
      public IntPtr ValueType;
      public IntPtr OwnerType;
    }

    public PropertyInfo (IntPtr pspec_ptr) {
      GParamSpec pspec = (GParamSpec) Marshal.PtrToStructure (pspec_ptr, typeof (GParamSpec));
      IntPtr name = g_param_spec_get_name (pspec_ptr);
      IntPtr nick = g_param_spec_get_nick (pspec_ptr);
      IntPtr blurb = g_param_spec_get_blurb (pspec_ptr);

      this.name = GLib.Marshaller.Utf8PtrToString (name);
      this.nick = GLib.Marshaller.Utf8PtrToString (nick);
      this.blurb = GLib.Marshaller.Utf8PtrToString (blurb);

      this.readable = ( (pspec.Flags & (1 << 0)) != 0);
      this.writeable = ( (pspec.Flags & (1 << 1)) != 0);
      this.controllable = ( (pspec.Flags & (1 << 9)) != 0);
      /* TODO: Add more flags later, like the mutable flags */

      this.gtype = new GLib.GType (pspec.ValueType);
      this.type = (System.Type) this.gtype;

      this.dflt = this.min = this.max = null;

      try {
        GLib.Value v = new GLib.Value (new GLib.GType (pspec.ValueType));
        g_param_value_set_default (pspec_ptr, ref v);
        this.dflt = v.Val;
        v.Dispose ();

        if (EnumInfo.IsEnumType (this.gtype)) {
          EnumInfo ei = new EnumInfo (this.gtype);
          this.min = ei.Min;
          this.max = ei.Max;
        } else {
          GLib.Value min = new GLib.Value (new GLib.GType (pspec.ValueType));
          GLib.Value max = new GLib.Value (new GLib.GType (pspec.ValueType));
          if (gstsharp_g_param_spec_get_range (pspec_ptr, ref min, ref max)) {
            this.min = (object) min.Val;
            this.max = (object) max.Val;
          }
          min.Dispose ();
          max.Dispose ();
        }
      } catch (Exception) {}
    }
  }

}
