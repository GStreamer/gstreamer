//
// Application.cs - Gst initialization
//
// Author: Alp Toker <alp@atoker.com>
//
// 2002 (C) Copyright, Alp Toker
//


namespace Gst {

	using System;
	using System.Runtime.InteropServices;

	public class Application {

                //
                // Disables creation of instances.
                //
                private Application ()
                {
                }

		[DllImport("gstreamer-0.10")]
		static extern void gst_init (ref int argc, ref IntPtr argv);

		[DllImport("gstreamer-0.10")]
		static extern bool gst_init_check (ref int argc, ref IntPtr argv);

		public static void Init ()
		{
			IntPtr argv = new IntPtr(0);
			int argc = 0;

			gst_init (ref argc, ref argv);
		}

                static bool do_init (string progname, ref string[] args, bool check)
                {
                        bool res = false;
                        string[] progargs = new string[args.Length + 1];

			progargs[0] = progname;
			args.CopyTo (progargs, 1);

			GLib.Argv argv = new GLib.Argv (progargs);
			IntPtr buf = argv.Handle;
			int argc = progargs.Length;

                        if (check)
                                res = gst_init_check (ref argc, ref buf);
                        else
                                gst_init (ref argc, ref buf);

			if (buf != argv.Handle)
				throw new Exception ("init returned new argv handle");

                        // copy back the resulting argv, minus argv[0], which we're
                        // not interested in.

			if (argc <= 1)
				args = new string[0];
			else {
				progargs = argv.GetArgs (argc);
				args = new string[argc - 1];
				Array.Copy (progargs, 1, args, 0, argc - 1);
			}

                        return res;
                }

                public static void Init (string progname, ref string[] args)
                {
                        do_init (progname, ref args, false);
                }
                                                                                                                       
                public static bool InitCheck (string progname, ref string[] args)
                {
                        return do_init (progname, ref args, true);
                }

		[DllImport("gstreamer-0.10")]
		static extern void gst_version (out uint major, out uint minor, out uint micro);

		public static string Version {
			get {
				uint major, minor, micro;
				gst_version (out major, out minor, out micro);
				return major + "." + minor + "." + micro;
			}
		}
	}
}
