//
// Application.cs: Framework initialization for GStreamer
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
//   Alp Toker (alp@atoker.com)
//
// (C) 2006 Novell, Inc.
// (C) 2002 Alp Toker
//

using System;
using System.Runtime.InteropServices;

namespace Gst 
{
    public static class Application 
    {
        public static void Init()
        {
            IntPtr argv = new IntPtr(0);
            int argc = 0;

            gst_init(ref argc, ref argv);
        }

        public static void Init(string progname, ref string [] args)
        {
            FullInit(progname, ref args, false);
        }
        
        public static void InitCheck(string progname, ref string [] args)
        {
            FullInit(progname, ref args, true);
        }
        
        public static void Deinit()
        {
            gst_deinit();
        }
        
        private static void FullInit(string progname, ref string [] args, bool check)
        {
            string [] progargs = new string[args.Length + 1];

            progargs[0] = progname;
            args.CopyTo(progargs, 1);

            GLib.Argv argv = new GLib.Argv(progargs);
            IntPtr argv_ptr = argv.Handle;
            int argc = progargs.Length;

            if(check) {
                IntPtr error_ptr;
                bool result = gst_init_check(ref argc, ref argv_ptr, out error_ptr);
                
                if(error_ptr != IntPtr.Zero) {
                    throw new ApplicationException("gst_init_check set error");
                } else if(!result) {
                    throw new ApplicationException("gst_init_check failed, error not set");
                }
            } else {
                gst_init(ref argc, ref argv_ptr);
            }
                
            if(argv_ptr != argv.Handle) {
                string init_call = check ? "gst_init_check" : "gst_init";
                throw new ApplicationException(init_call + " returned a new argv handle");
            }
            
            if(argc <= 1) {
                args = new string[0];
            } else {
                progargs = argv.GetArgs(argc);
                args = new string[argc - 1];
                Array.Copy(progargs, 1, args, 0, argc - 1);
            }
        }
        
        private static Version version = null;
        
        public static Version Version {
            get {
                if(version == null) {
                    version = new Version();
                }
                
                return version;
            }
        }
        
        [DllImport("gstreamer-0.10")]
        private static extern void gst_init(ref int argc, ref IntPtr argv);

        [DllImport("gstreamer-0.10")]
        private static extern bool gst_init_check(ref int argc, ref IntPtr argv, out IntPtr error);
        
        [DllImport("gstreamer-0.10")]
        private static extern void gst_deinit();
    }
}
