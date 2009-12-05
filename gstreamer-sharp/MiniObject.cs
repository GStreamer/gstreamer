// MiniObject.cs - GstMiniObject class wrapper implementation
//
// Authors: Mike Kestner <mkestner@speakeasy.net>
//          Sebastian Dröge <sebastian.droege@collabora.co.uk>
//
// Copyright (c) 2001-2003 Mike Kestner
// Copyright (c) 2004-2005 Novell, Inc.
// Copyright (c) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of version 2 of the Lesser GNU General
// Public License as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this program; if not, write to the
// Free Software Foundation, Inc., 59 Temple Place - Suite 330,
// Boston, MA 02111-1307, USA.

// Based on Object.cs from Gtk# 2.8.3

// TODO: For managed types, install finalizer in ThresholdType
// and only destroy the managed instance if the native instance
// gets finalized => move managed instance to managed_tb_destroyed
// and unref, if finalizer is called remove it completely.
// For non-managed types handle as is.

namespace Gst {

  using System;
  using System.Collections;
  using System.ComponentModel;
  using System.Reflection;
  using System.Runtime.InteropServices;
  using System.Text;
  using Gst.GLib;

  public class MiniObject : IWrapper, IDisposable {
    [StructLayout (LayoutKind.Sequential) ]
    struct GTypeClass {
      public IntPtr gtype;
    }

    [StructLayout (LayoutKind.Sequential) ]
    struct GstMiniObjectClass {
      GTypeClass parent;
      IntPtr copy;
      IntPtr finalize;
      IntPtr reserved;
    }

    [StructLayout (LayoutKind.Sequential) ]
    struct GTypeInstance {
      public IntPtr g_class;
    }

    [StructLayout (LayoutKind.Sequential) ]
    struct GstMiniObject {
      GTypeInstance parent;
      public int refcount;
      public Gst.MiniObjectFlags flags;
      IntPtr reserved;
    }

    IntPtr handle;
    bool disposed = false;
    static Hashtable Objects = new Hashtable();

    ~MiniObject () {
      Dispose ();
    }

    [DllImport ("libgstreamer-0.10.dll") ]
    static extern void gst_mini_object_unref (IntPtr raw);

    public virtual void Dispose () {
      if (disposed)
        return;

      disposed = true;
      lock (typeof (MiniObject)) {
        if (handle != IntPtr.Zero) {
          Objects.Remove (handle);
          try {
            gst_mini_object_unref (handle);
          } catch (Exception e) {
            Console.WriteLine ("Exception while disposing a " + this + " in Gtk#");
            throw e;
          }
          handle = IntPtr.Zero;
        }
      }
      GC.SuppressFinalize (this);
    }

    [DllImport ("libgstreamer-0.10.dll") ]
    static extern IntPtr gst_mini_object_ref (IntPtr raw);

    public static MiniObject GetObject (IntPtr o, bool owned_ref) {
      if (o == IntPtr.Zero)
        return null;

      MiniObject obj = null;
      lock (typeof (MiniObject)) {
        WeakReference weak_ref = Objects[o] as WeakReference;

        if (weak_ref != null && weak_ref.IsAlive)
          obj = weak_ref.Target as MiniObject;

        if (obj == null)
          obj = Objects[o] as MiniObject;
      }

      if (obj != null && obj.handle == o) {
        if (owned_ref)
          gst_mini_object_unref (obj.handle);
        obj.disposed = false;
        return obj;
      }

      obj = CreateObject (o);
      if (obj == null)
        return null;

      if (!owned_ref)
        gst_mini_object_ref (obj.Handle);
      Objects [o] = new WeakReference (obj);
      return obj;
    }

    static BindingFlags flags = BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.Instance | BindingFlags.CreateInstance;
    private static MiniObject CreateObject (IntPtr raw) {
      if (raw == IntPtr.Zero)
        return null;

      Type type = GetTypeOrParent (raw);

      if (type == null)
        return null;

      MiniObject obj;
      try {
        obj = Activator.CreateInstance (type, flags, null, new object[] {raw}, null) as Gst.MiniObject;
      } catch (MissingMethodException) {
        throw new Gst.GLib.MissingIntPtrCtorException ("Gst.MiniObject subclass " + type + " must provide a protected or public IntPtr ctor to support wrapping of native object handles.");
      }
      return obj;
    }

    [DllImport ("gstreamersharpglue-0.10.dll") ]
    static extern IntPtr gstsharp_g_type_from_instance (IntPtr inst);

    static Type GetTypeOrParent (IntPtr obj) {
      IntPtr typeid = gstsharp_g_type_from_instance (obj);
      if (typeid == GType.Invalid.Val)
        return null;

      Type result = GType.LookupType (typeid);
      while (result == null) {
        typeid = g_type_parent (typeid);
        if (typeid == IntPtr.Zero)
          return null;
        result = GType.LookupType (typeid);
      }
      return result;
    }

    [DllImport ("libgobject-2.0-0.dll") ]
    static extern IntPtr g_type_parent (IntPtr typ);

    public static MiniObject GetObject (IntPtr o) {
      return GetObject (o, false);
    }

    private static void ConnectDefaultHandlers (GType gtype, System.Type t) {
      foreach (MethodInfo minfo in t.GetMethods (BindingFlags.Instance | BindingFlags.NonPublic | BindingFlags.Public | BindingFlags.DeclaredOnly)) {
        MethodInfo baseinfo = minfo.GetBaseDefinition ();
        if (baseinfo == minfo)
          continue;

        foreach (object attr in baseinfo.GetCustomAttributes (typeof (DefaultSignalHandlerAttribute), false)) {
          DefaultSignalHandlerAttribute sigattr = attr as DefaultSignalHandlerAttribute;
          MethodInfo connector = sigattr.Type.GetMethod (sigattr.ConnectionMethod, BindingFlags.Static | BindingFlags.NonPublic);
          object[] parms = new object [1];
          parms [0] = gtype;
          connector.Invoke (null, parms);
          break;
        }
      }

    }

    private static void InvokeClassInitializers (GType gtype, System.Type t) {
      object[] parms = {gtype, t};

      BindingFlags flags = BindingFlags.Static | BindingFlags.NonPublic;

      foreach (TypeInitializerAttribute tia in t.GetCustomAttributes (typeof (TypeInitializerAttribute), true)) {
        MethodInfo m = tia.Type.GetMethod (tia.MethodName, flags);
        if (m != null)
          m.Invoke (null, parms);
      }
    }

    static Hashtable class_structs;

    static GstMiniObjectClass GetClassStruct (Gst.GLib.GType gtype, bool use_cache) {
      if (class_structs == null)
        class_structs = new Hashtable ();

      if (use_cache && class_structs.Contains (gtype))
        return (GstMiniObjectClass) class_structs [gtype];
      else {
        IntPtr class_ptr = gtype.GetClassPtr ();
        GstMiniObjectClass class_struct = (GstMiniObjectClass) Marshal.PtrToStructure (class_ptr, typeof (GstMiniObjectClass));
        if (use_cache)
          class_structs.Add (gtype, class_struct);
        return class_struct;
      }
    }

    static void OverrideClassStruct (Gst.GLib.GType gtype, GstMiniObjectClass class_struct) {
      IntPtr class_ptr = gtype.GetClassPtr ();
      Marshal.StructureToPtr (class_struct, class_ptr, false);
    }

    static int type_uid;
    static string BuildEscapedName (System.Type t) {
      string qn = t.FullName;
      // Just a random guess
      StringBuilder sb = new StringBuilder (20 + qn.Length);
      sb.Append ("__gtksharp_");
      sb.Append (type_uid++);
      sb.Append ("_");
      foreach (char c in qn) {
        if ( (c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))
          sb.Append (c);
        else if (c == '.')
          sb.Append ('_');
        else if ( (uint) c <= byte.MaxValue) {
          sb.Append ('+');
          sb.Append ( ( (byte) c).ToString ("x2"));
        } else {
          sb.Append ('-');
          sb.Append ( ( (uint) c).ToString ("x4"));
        }
      }
      return sb.ToString ();
    }


    [StructLayout (LayoutKind.Sequential) ]
    struct GTypeInfo {
      public ushort class_size;
      IntPtr base_init;
      IntPtr base_finalize;
      IntPtr class_init;
      IntPtr class_finalize;
      IntPtr class_data;
      public ushort instance_size;
      ushort n_preallocs;
      IntPtr instance_init;
      IntPtr value_table;
    }

    [StructLayout (LayoutKind.Sequential) ]
    struct GTypeQuery {
      public IntPtr type;
      public IntPtr type_name;
      public uint class_size;
      public uint instance_size;
    }


    [DllImport ("libgobject-2.0-0.dll") ]
    static extern void g_type_query (IntPtr type, out GTypeQuery query);

    [DllImport ("libgobject-2.0-0.dll") ]
    static extern IntPtr g_type_register_static (IntPtr parent, IntPtr name, ref GTypeInfo info, int flags);

    private static GType RegisterGType (System.Type t) {
      GType parent_gtype = LookupGType (t.BaseType);
      string name = BuildEscapedName (t);
      IntPtr native_name = Gst.GLib.Marshaller.StringToPtrGStrdup (name);
      GTypeQuery query;
      g_type_query (parent_gtype.Val, out query);
      GTypeInfo info = new GTypeInfo ();
      info.class_size = (ushort) query.class_size;
      info.instance_size = (ushort) query.instance_size;
      GType gtype = new GType (g_type_register_static (parent_gtype.Val, native_name, ref info, 0));
      Gst.GLib.Marshaller.Free (native_name);

      Gst.GLib.GType.Register (gtype, t);

      ConnectDefaultHandlers (gtype, t);
      InvokeClassInitializers (gtype, t);
      return gtype;
    }

    protected GType LookupGType () {
      if (Handle != IntPtr.Zero) {
        GTypeInstance obj = (GTypeInstance) Marshal.PtrToStructure (Handle, typeof (GTypeInstance));
        GTypeClass klass = (GTypeClass) Marshal.PtrToStructure (obj.g_class, typeof (GTypeClass));
        return new Gst.GLib.GType (klass.gtype);
      } else {
        return LookupGType (GetType ());
      }
    }

    protected internal static GType LookupGType (System.Type t) {
      GType gtype = (GType) t;
      if (gtype.ToString () != "GtkSharpValue")
        return gtype;

      return RegisterGType (t);
    }

    protected MiniObject (IntPtr raw) {
      Raw = raw;
    }

    protected MiniObject () {
      CreateNativeObject ();
    }

    [DllImport ("libgstreamer-0.10.dll") ]
    static extern IntPtr gst_mini_object_new (IntPtr gtype);

    protected virtual void CreateNativeObject () {
      Raw = gst_mini_object_new (LookupGType ().Val);
      Objects [handle] = this;
    }

    protected virtual IntPtr Raw {
      get {
        return handle;
      } set {
        if (handle != IntPtr.Zero)
          Objects.Remove (handle);
        handle = value;
        if (value == IntPtr.Zero)
          return;
        Objects [value] = new WeakReference (this);
      }
    }

    [DllImport ("libgstreamer-0.10.dll") ]
    static extern IntPtr gst_mini_object_get_type();

    public static Gst.GLib.GType GType {
      get {
        IntPtr raw_ret = gst_mini_object_get_type();
        Gst.GLib.GType ret = new Gst.GLib.GType (raw_ret);
        return ret;
      }
    }

    protected string TypeName {
      get {
        return NativeType.ToString();
      }
    }

    internal Gst.GLib.GType NativeType {
      get {
        return LookupGType ();
      }
    }

    public IntPtr Handle {
      get {
        return handle;
      }
    }

    public IntPtr OwnedHandle {
      get {
        return gst_mini_object_ref (handle);
      }
    }

    public override int GetHashCode () {
      return Handle.GetHashCode ();
    }

    [DllImport ("libgobject-2.0-0.dll") ]
    static extern bool g_type_check_instance_is_a (IntPtr obj, IntPtr gtype);

    internal static bool IsMiniObject (IntPtr obj) {
      return g_type_check_instance_is_a (obj, MiniObject.GType.Val);
    }

    internal int Refcount {
      get {
        GstMiniObject inst_struct = (GstMiniObject) Marshal.PtrToStructure (Handle, typeof (GstMiniObject));
        return inst_struct.refcount;
      }
    }

    public Gst.MiniObjectFlags Flags {
      get {
        GstMiniObject inst_struct = (GstMiniObject) Marshal.PtrToStructure (Handle, typeof (GstMiniObject));
        return inst_struct.flags;
      } set {
        GstMiniObject inst_struct = (GstMiniObject) Marshal.PtrToStructure (Handle, typeof (GstMiniObject));
        inst_struct.flags = value;
      }
    }

    [DllImport ("libgstreamer-0.10.dll") ]
    static extern bool gst_mini_object_is_writable (IntPtr raw);

    public bool IsWritable {
      get {
        bool raw_ret = gst_mini_object_is_writable (Handle);
        bool ret = raw_ret;
        return ret;
      }
    }

    [DllImport ("libgstreamer-0.10.dll") ]
    private static extern IntPtr gst_value_dup_mini_object (ref Gst.GLib.Value v);

    public MiniObject (Gst.GLib.Value val) : base () {
      Raw = gst_value_dup_mini_object (ref val);
    }

    [DllImport ("libgstreamer-0.10.dll") ]
    private static extern void gst_value_set_mini_object (ref Gst.GLib.Value v, IntPtr o);

    public static explicit operator Gst.GLib.Value (MiniObject o) {
      Gst.GLib.Value val = new Gst.GLib.Value (o.LookupGType ());

      gst_value_set_mini_object (ref val, o.Handle);

      return val;
    }

    public void SetGValue (ref Gst.GLib.Value val) {
      gst_value_set_mini_object (ref val, Handle);
    }

    /* FIXME: This is not optimal */
    public void MakeWritable() {
      if (IsWritable)
        return;

      IntPtr old = Handle;
      IntPtr copy = gst_mini_object_copy (Handle);
      Raw = copy;
      gst_mini_object_unref (old);
    }

    [DllImport ("libgstreamer-0.10.dll") ]
    static extern IntPtr gst_mini_object_copy (IntPtr raw);

    public Gst.MiniObject Copy() {
      IntPtr raw_ret = gst_mini_object_copy (Handle);
      return GetObject (raw_ret, true);
    }
  }
}
