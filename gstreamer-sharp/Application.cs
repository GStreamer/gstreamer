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
      }
      set {
        type_name = value;
      }
    }
  }

  public static class Application {
    public static void Init() {
      IntPtr argv = new IntPtr (0);
      int argc = 0;

      gst_init (ref argc, ref argv);
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

    private static System.Type GstTypeMapping (GLib.GType gtype, string gtype_name) {
      Assembly[] assemblies = (Assembly[]) AppDomain.CurrentDomain.GetAssemblies ().Clone ();

      foreach (Assembly asm in assemblies) {
        Type[] ts = asm.GetTypes ();
        foreach (Type t in ts) {
          if (t.IsDefined (typeof (Gst.GTypeNameAttribute), false)) {
            GTypeNameAttribute gattr = (GTypeNameAttribute) Attribute.GetCustomAttribute (t, typeof (GTypeNameAttribute), false);
            if (gtype_name.Equals (gattr.TypeName)) {
              return t;
            }
          }
        }
      }

      foreach (Assembly asm in assemblies) {
        foreach (AssemblyName ref_name in asm.GetReferencedAssemblies ()) {
          string asm_dir = Path.GetDirectoryName (asm.Location);
          try {
            Assembly ref_asm;
            if (File.Exists (Path.Combine (asm_dir, ref_name.Name + ".dll")))
              ref_asm = Assembly.LoadFrom (Path.Combine (asm_dir, ref_name.Name + ".dll"));
            else
              ref_asm = Assembly.Load (ref_name);

            Type[] ts = asm.GetTypes ();
            foreach (Type t in ts) {
              if (t.IsDefined (typeof (Gst.GTypeNameAttribute), false)) {
                GTypeNameAttribute gattr = (GTypeNameAttribute) Attribute.GetCustomAttribute (t, typeof (GTypeNameAttribute), false);
                if (gtype_name.Equals (gattr.TypeName)) {
                  return t;
                }
              }
            }

          } catch (Exception) {
            /* Failure to load a referenced assembly is not an error */
          }
        }
      }

      return null;
    }

    private static void RegisterManagedTypes() {
      GLib.GType.GTypeMapping += GstTypeMapping;

      GLib.GType t;

      t = Gst.Fraction.GType;
      t = Gst.DoubleRange.GType;
      t = Gst.IntRange.GType;
      t = Gst.FractionRange.GType;
      t = Gst.Fourcc.GType;
      t = Gst.Date.GType;
      t = Gst.List.GType;
      t = Gst.Array.GType;
      t = Gst.Caps.GType;
      t = Gst.Structure.GType;
      t = Gst.TagList.GType;
      t = Gst.MiniObject.GType;

      GtkSharp.GstreamerSharp.ObjectManager.Initialize ();
    }

    private static void FullInit (string progname, ref string [] args, bool check) {
      string [] progargs = new string[args.Length + 1];

      progargs[0] = progname;
      args.CopyTo (progargs, 1);

      GLib.Argv argv = new GLib.Argv (progargs);
      IntPtr argv_ptr = argv.Handle;
      int argc = progargs.Length;

      if (check) {
        IntPtr error_ptr;
        bool result = gst_init_check (ref argc, ref argv_ptr, out error_ptr);

        if (error_ptr != IntPtr.Zero) {
          throw new GLib.GException (error_ptr);
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

      if (argc <= 1) {
        args = new string[0];
      } else {
        progargs = argv.GetArgs (argc);
        args = new string[argc - 1];
        System.Array.Copy (progargs, 1, args, 0, argc - 1);
      }
      RegisterManagedTypes ();
    }

    [DllImport ("gstreamer-0.10.dll") ]
    private static extern void gst_init (ref int argc, ref IntPtr argv);

    [DllImport ("gstreamer-0.10.dll") ]
    private static extern bool gst_init_check (ref int argc, ref IntPtr argv, out IntPtr error);

    [DllImport ("gstreamer-0.10.dll") ]
    private static extern void gst_deinit();
  }
}
