//
// Application.cs: Framework initialization for GStreamer
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
//   Alp Toker (alp@atoker.com)
//
// Copyright (C) 2002 Alp Toker
// Copyright (C) 2006 Novell, Inc.
// Copyright (C) 2009 Sebastian Dröge <sebastian.droege@collabora.co.uk>
//

using System;
using System.Reflection;
using System.IO;
using System.Runtime.InteropServices;
using System.Collections.Generic;

namespace Gst {

  [AttributeUsage (AttributeTargets.Enum | AttributeTargets.Class | AttributeTargets.Struct) ]
  public sealed class GTypeNameAttribute : Attribute {
    string type_name;

    public GTypeNameAttribute (string gtype_name) {
      this.type_name = gtype_name;
    }

    public string TypeName {
      get {
        return type_name;
      } set {
        type_name = value;
      }
    }
  }

  public static class Application {
    public static void Init() {
      IntPtr argv = new IntPtr (0);
      int argc = 0;

      gst_init (ref argc, ref argv);
      gst_controller_init (ref argc, ref argv);
      gst_pb_utils_init ();
      RegisterManagedTypes ();
    }

    public static void Init (string progname, ref string [] args) {
      FullInit (progname, ref args, false);
    }

    public static void InitCheck (string progname, ref string [] args) {
      FullInit (progname, ref args, true);
    }

    public static void Deinit() {
      gst_deinit();
    }

    private static Dictionary<int,bool> AssemblyTypesInCache = new Dictionary<int,bool> ();
    private static Dictionary<string,Type> TypeCache = new Dictionary<string,Type> ();

    // Recursively check for types with GTypeNameAttribute and put them in TypeCache,
    // but only gstreamer-sharp is in the chain of referenced assemblies.
    private static bool PutAssemblyTypesInCache (Assembly asm)
    {
      bool result;

      // If already visited, return immediately
      if (AssemblyTypesInCache.TryGetValue(asm.GetHashCode (), out result))
        return result;

      result = false;
      AssemblyTypesInCache.Add (asm.GetHashCode (), false);

      // Result is true for gstreamer-sharp or if a referenced assembly results in true
      if (asm.GetName().Name == "gstreamer-sharp")
        result = true;
      foreach (AssemblyName ref_name in asm.GetReferencedAssemblies ()) {
        try {
          result = result | PutAssemblyTypesInCache (Assembly.Load (ref_name));
        } catch {
          /* Failure to load a referenced assembly is not an error */
        }
      }

      // Add types with GTypeNameAttribute in TypeCache
      if (result) {
        AssemblyTypesInCache[asm.GetHashCode ()] = true;
        Type[] ts = asm.GetTypes ();
        foreach (Type t in ts) {
          if (t.IsDefined (typeof (GTypeNameAttribute), false)) {
            GTypeNameAttribute gattr = (GTypeNameAttribute) Attribute.GetCustomAttribute (t, typeof (GTypeNameAttribute), false);
            TypeCache[gattr.TypeName] = t;
          }
        }
      }

      return result;
    }

    private static System.Type GstResolveType (Gst.GLib.GType gtype, string gtype_name) {
      // Make sure all loaded assemblies are in the TypeCache
      Assembly[] assemblies = (Assembly[]) AppDomain.CurrentDomain.GetAssemblies ().Clone ();
      foreach (Assembly asm in assemblies) {
        PutAssemblyTypesInCache (asm);
      }

      // Return the managed type
      if (TypeCache.ContainsKey (gtype_name))
        return TypeCache[gtype_name];

      return null;
    }

    private static void RegisterManagedTypes() {
      Gst.GLib.GType.ResolveType += GstResolveType;

      Gst.GLib.GType.Register (Fraction.GType, typeof (Fraction));
      Gst.GLib.GType.Register (IntRange.GType, typeof (IntRange));
      Gst.GLib.GType.Register (DoubleRange.GType, typeof (DoubleRange));
      Gst.GLib.GType.Register (FractionRange.GType, typeof (FractionRange));
      Gst.GLib.GType.Register (Fourcc.GType, typeof (Fourcc));
      Gst.GLib.GType.Register (Date.GType, typeof (Date));
      Gst.GLib.GType.Register (List.GType, typeof (List));
      Gst.GLib.GType.Register (Array.GType, typeof (Array));
      Gst.GLib.GType.Register (Caps.GType, typeof (Caps));
      Gst.GLib.GType.Register (Structure.GType, typeof (Structure));
      Gst.GLib.GType.Register (TagList.GType, typeof (TagList));
      Gst.GLib.GType.Register (MiniObject.GType, typeof (MiniObject));
      Gst.GLib.GType.Register (Bus.GType, typeof (Bus));
      Gst.GLib.GType.Register (Pad.GType, typeof (Pad));
      Gst.GLib.GType.Register (GhostPad.GType, typeof (GhostPad));
      Gst.GLib.GType.Register (Message.GType, typeof (Message));
      Gst.GLib.GType.Register (SystemClock.GType, typeof (SystemClock));

      GtkSharp.GstreamerSharp.ObjectManager.Initialize ();
    }

    private static void FullInit (string progname, ref string [] args, bool check) {
      string [] progargs = new string[args.Length + 1];

      progargs[0] = progname;
      args.CopyTo (progargs, 1);

      Gst.GLib.Argv argv = new Gst.GLib.Argv (progargs);
      IntPtr argv_ptr = argv.Handle;
      int argc = progargs.Length;

      if (check) {
        IntPtr error_ptr;
        bool result = gst_init_check (ref argc, ref argv_ptr, out error_ptr);

        if (error_ptr != IntPtr.Zero) {
          throw new Gst.GLib.GException (error_ptr);
        } else if (!result) {
          throw new ApplicationException ("gst_init_check() failed: Reason unknown");
        }
      } else {
        gst_init (ref argc, ref argv_ptr);
      }

      if (argv_ptr != argv.Handle) {
        string init_call = check ? "gst_init_check()" : "gst_init()";
        throw new ApplicationException (init_call + " returned a new argv handle");
      }

      gst_controller_init (ref argc, ref argv_ptr);
      gst_pb_utils_init ();

      if (argc <= 1) {
        args = new string[0];
      } else {
        progargs = argv.GetArgs (argc);
        args = new string[argc - 1];
        System.Array.Copy (progargs, 1, args, 0, argc - 1);
      }
      RegisterManagedTypes ();
    }

    [DllImport ("libgstreamer-0.10.dll") ]
    private static extern void gst_init (ref int argc, ref IntPtr argv);

    [DllImport ("libgstreamer-0.10.dll") ]
    private static extern bool gst_init_check (ref int argc, ref IntPtr argv, out IntPtr error);

    [DllImport ("libgstcontroller-0.10.dll") ]
    private static extern void gst_controller_init (ref int argc, ref IntPtr argv);

    [DllImport ("libgstpbutils-0.10.dll") ]
    private static extern void gst_pb_utils_init ();

    [DllImport ("libgstreamer-0.10.dll") ]
    private static extern void gst_deinit();
  }
}
