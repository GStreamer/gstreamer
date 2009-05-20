using GLib;
using Gst;
using System;
using System.Runtime.InteropServices;

namespace Gst {

  public struct EnumValue {
    internal int value;
    public int Value {
      get {
        return value;
      }
    }

    internal string value_name;
    public string Name {
      get {
        return value_name;
      }
    }

    internal string value_nick;
    public string Nick {
      get {
        return value_nick;
      }
    }
  }

  public struct FlagsValue {
    internal uint value;
    public uint Value {
      get {
        return value;
      }
    }

    internal string value_name;
    public string Name {
      get {
        return value_name;
      }
    }

    internal string value_nick;
    public string Nick {
      get {
        return value_nick;
      }
    }
  }

  public struct EnumInfo {
    [StructLayout (LayoutKind.Sequential) ]
    struct GTypeClass {
      IntPtr gtype;
    }

    [StructLayout (LayoutKind.Sequential) ]
    struct GEnumClass {
      GTypeClass gclass;
      public int minimum;
      public int maximum;
      public uint n_values;
      public IntPtr values;
    }

    [StructLayout (LayoutKind.Sequential) ]
    struct GEnumValue {
      public int value;
      public IntPtr value_name;
      public IntPtr value_nick;
    }

    [DllImport ("libgobject-2.0-0.dll") ]
    static extern IntPtr g_type_class_ref (IntPtr gtype);
    [DllImport ("libgobject-2.0-0.dll") ]
    static extern void g_type_class_unref (IntPtr gclass);
    [DllImport ("libgobject-2.0-0.dll") ]
    static extern bool g_type_is_a (IntPtr type, IntPtr is_a_type);

    int min;
    public int Min {
      get {
        return min;
      }
    }

    int max;
    public int Max {
      get {
        return max;
      }
    }

    EnumValue[] values;
    public EnumValue[] Values {
      get {
        return values;
      }
    }

    public EnumValue this[int val] {
      get {
        foreach (EnumValue v in Values) {
          if (v.value == val)
            return v;
        }

        throw new Exception ();
      }
    }

    public static bool IsEnumType (GLib.GType gtype) {
      return (g_type_is_a (gtype.Val, GType.Enum.Val));
    }

    public EnumInfo (GLib.GType gtype) {
      if (!IsEnumType (gtype))
        throw new ArgumentException ();

      IntPtr class_ptr = g_type_class_ref (gtype.Val);
      if (class_ptr == IntPtr.Zero)
        throw new Exception ();

      GEnumClass klass = (GEnumClass) Marshal.PtrToStructure (class_ptr, typeof (GEnumClass));
      this.min = klass.minimum;
      this.max = klass.maximum;

      values = new EnumValue[klass.n_values];
      int unmanaged_struct_size = Marshal.SizeOf (typeof (GEnumValue));
      for (int i = 0; i < klass.n_values; i++) {
        GEnumValue gv = (GEnumValue) Marshal.PtrToStructure (new IntPtr (klass.values.ToInt64() + i * unmanaged_struct_size), typeof (GEnumValue));
        values[i].value = gv.value;
        values[i].value_name = GLib.Marshaller.Utf8PtrToString (gv.value_name);
        values[i].value_nick = GLib.Marshaller.Utf8PtrToString (gv.value_nick);
      }

      g_type_class_unref (class_ptr);
    }
  }

  public struct FlagsInfo {
    [StructLayout (LayoutKind.Sequential) ]
    struct GTypeClass {
      IntPtr gtype;
    }

    [StructLayout (LayoutKind.Sequential) ]
    struct GFlagsClass {
      GTypeClass gclass;
      public uint mask;
      public uint n_values;
      public IntPtr values;
    }

    [StructLayout (LayoutKind.Sequential) ]
    struct GFlagsValue {
      public uint value;
      public IntPtr value_name;
      public IntPtr value_nick;
    }

    [DllImport ("libgobject-2.0-0.dll") ]
    static extern IntPtr g_type_class_ref (IntPtr gtype);
    [DllImport ("libgobject-2.0-0.dll") ]
    static extern void g_type_class_unref (IntPtr gclass);
    [DllImport ("libgobject-2.0-0.dll") ]
    static extern bool g_type_is_a (IntPtr type, IntPtr is_a_type);

    uint mask;
    public uint Mask {
      get {
        return mask;
      }
    }

    FlagsValue[] values;
    public FlagsValue[] Values {
      get {
        return values;
      }
    }

    public static bool IsFlagsType (GLib.GType gtype) {
      return (g_type_is_a (gtype.Val, GType.Flags.Val));
    }

    public FlagsValue[] this[uint flags] {
      get {
        System.Collections.ArrayList ret = new System.Collections.ArrayList ();

        foreach (FlagsValue v in Values) {
          if (flags == 0 && v.value == 0)
            ret.Add (v);
          else if ( (v.value & flags) != 0)
            ret.Add (v);
        }

        return (FlagsValue[]) ret.ToArray (typeof (FlagsValue));
      }
    }

    public FlagsInfo (GLib.GType gtype) {
      if (!IsFlagsType (gtype))
        throw new ArgumentException ();

      IntPtr class_ptr = g_type_class_ref (gtype.Val);
      if (class_ptr == IntPtr.Zero)
        throw new Exception ();

      GFlagsClass klass = (GFlagsClass) Marshal.PtrToStructure (class_ptr, typeof (GFlagsClass));
      this.mask = klass.mask;

      values = new FlagsValue[klass.n_values];
      int unmanaged_struct_size = Marshal.SizeOf (typeof (GFlagsValue));
      for (int i = 0; i < klass.n_values; i++) {
        GFlagsValue gv = (GFlagsValue) Marshal.PtrToStructure (new IntPtr (klass.values.ToInt64() + i * unmanaged_struct_size), typeof (GFlagsValue));
        values[i].value = gv.value;
        values[i].value_name = GLib.Marshaller.Utf8PtrToString (gv.value_name);
        values[i].value_nick = GLib.Marshaller.Utf8PtrToString (gv.value_nick);
      }

      g_type_class_unref (class_ptr);
    }
  }
}
