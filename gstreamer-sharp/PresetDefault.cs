using System;
using System.Runtime.InteropServices;

namespace Gst {
  public static class PresetDefault {
    [StructLayout (LayoutKind.Sequential) ]
    struct GstPresetInterface {
      public GetPresetNamesNativeDelegate GetPresetNames;
      public GetPropertyNamesNativeDelegate GetPropertyNames;
      public LoadPresetNativeDelegate LoadPreset;
      public SavePresetNativeDelegate SavePreset;
      public RenamePresetNativeDelegate RenamePreset;
      public DeletePresetNativeDelegate DeletePreset;
      public SetMetaNativeDelegate SetMeta;
      public GetMetaNativeDelegate GetMeta;
      [MarshalAs (UnmanagedType.ByValArray, SizeConst=4) ]
      public IntPtr[] GstReserved;
    }
    delegate IntPtr GetPresetNamesNativeDelegate (IntPtr inst);
    delegate IntPtr GetPropertyNamesNativeDelegate (IntPtr inst);
    delegate bool LoadPresetNativeDelegate (IntPtr inst, IntPtr name);
    delegate bool SavePresetNativeDelegate (IntPtr inst, IntPtr name);
    delegate bool RenamePresetNativeDelegate (IntPtr inst, IntPtr old_name, IntPtr new_name);
    delegate bool DeletePresetNativeDelegate (IntPtr inst, IntPtr name);
    delegate bool SetMetaNativeDelegate (IntPtr inst, IntPtr name, IntPtr tag, IntPtr value);
    delegate bool GetMetaNativeDelegate (IntPtr inst, IntPtr name, IntPtr tag, out IntPtr value);

    static GstPresetInterface default_iface;

    [DllImport ("libgobject-2.0-0.dll") ]
    static extern IntPtr g_type_default_interface_ref (IntPtr type);
    [DllImport ("libgstreamer-0.10.dll") ]
    static extern IntPtr gst_preset_get_type();

    static PresetDefault () {
      IntPtr type = gst_preset_get_type ();
      if (type == IntPtr.Zero)
        throw new Exception ("Can't get GstPreset interface type");
      IntPtr native_iface = g_type_default_interface_ref (type);
      if (native_iface == IntPtr.Zero)
        throw new Exception ("Can't get GstPreset default interface vtable");
      default_iface = (GstPresetInterface) Marshal.PtrToStructure (native_iface, typeof (GstPresetInterface));
    }

    public static bool DeletePreset (Gst.GLib.Object o, string name) {
      IntPtr native_name = Gst.GLib.Marshaller.StringToPtrGStrdup (name);
      bool raw_ret = default_iface.DeletePreset (o.Handle, native_name);
      bool ret = raw_ret;
      Gst.GLib.Marshaller.Free (native_name);
      return ret;
    }

    public static string[] GetPropertyNames (Gst.GLib.Object o) {
      IntPtr raw_ret = default_iface.GetPropertyNames (o.Handle);
      string[] ret = Gst.GLib.Marshaller.NullTermPtrToStringArray (raw_ret, true);
      return ret;
    }

    public static bool RenamePreset (Gst.GLib.Object o, string old_name, string new_name) {
      IntPtr native_old_name = Gst.GLib.Marshaller.StringToPtrGStrdup (old_name);
      IntPtr native_new_name = Gst.GLib.Marshaller.StringToPtrGStrdup (new_name);
      bool raw_ret = default_iface.RenamePreset (o.Handle, native_old_name, native_new_name);
      bool ret = raw_ret;
      Gst.GLib.Marshaller.Free (native_old_name);
      Gst.GLib.Marshaller.Free (native_new_name);
      return ret;
    }

    public static bool SetMeta (Gst.GLib.Object o, string name, string tag, string value) {
      IntPtr native_name = Gst.GLib.Marshaller.StringToPtrGStrdup (name);
      IntPtr native_tag = Gst.GLib.Marshaller.StringToPtrGStrdup (tag);
      IntPtr native_value = Gst.GLib.Marshaller.StringToPtrGStrdup (value);
      bool raw_ret = default_iface.SetMeta (o.Handle, native_name, native_tag, native_value);
      bool ret = raw_ret;
      Gst.GLib.Marshaller.Free (native_name);
      Gst.GLib.Marshaller.Free (native_tag);
      Gst.GLib.Marshaller.Free (native_value);
      return ret;
    }

    public static bool LoadPreset (Gst.GLib.Object o, string name) {
      IntPtr native_name = Gst.GLib.Marshaller.StringToPtrGStrdup (name);
      bool raw_ret = default_iface.LoadPreset (o.Handle, native_name);
      bool ret = raw_ret;
      Gst.GLib.Marshaller.Free (native_name);
      return ret;
    }

    public static bool GetMeta (Gst.GLib.Object o, string name, string tag, out string value) {
      IntPtr native_name = Gst.GLib.Marshaller.StringToPtrGStrdup (name);
      IntPtr native_tag = Gst.GLib.Marshaller.StringToPtrGStrdup (tag);
      IntPtr native_value;
      bool raw_ret = default_iface.GetMeta (o.Handle, native_name, native_tag, out native_value);
      bool ret = raw_ret;
      Gst.GLib.Marshaller.Free (native_name);
      Gst.GLib.Marshaller.Free (native_tag);
      value = Gst.GLib.Marshaller.PtrToStringGFree (native_value);
      return ret;
    }

    public static string[] GetPresetNames (Gst.GLib.Object o) {
      IntPtr raw_ret = default_iface.GetPresetNames (o.Handle);
      string[] ret = Gst.GLib.Marshaller.NullTermPtrToStringArray (raw_ret, true);
      return ret;
    }

    public static bool SavePreset (Gst.GLib.Object o, string name) {
      IntPtr native_name = Gst.GLib.Marshaller.StringToPtrGStrdup (name);
      bool raw_ret = default_iface.SavePreset (o.Handle, native_name);
      bool ret = raw_ret;
      Gst.GLib.Marshaller.Free (native_name);
      return ret;
    }
  }
}
