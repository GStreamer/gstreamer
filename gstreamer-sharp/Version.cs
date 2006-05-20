//
// Version.cs: Lightweight Version Object for GStreamer
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
//
// (C) 2006 Novell, Inc.
//

using System;
using System.Runtime.InteropServices;

namespace Gst 
{
    public class Version
    {
        private uint major;
        private uint minor;
        private uint micro;
        private uint nano;
        private string version_string;
    
        internal Version()
        {
            gst_version(out major, out minor, out micro, out nano);
        }
        
        public override string ToString()
        {
            return String.Format("{0}.{1}.{2}.{3}", major, minor, micro, nano);
        }
        
        public string Description {
            get {
                if(version_string == null) {
                    IntPtr version_string_ptr = gst_version_string();
                    version_string = GLib.Marshaller.Utf8PtrToString(version_string_ptr);
                }
            
                return version_string;
            }
        }
        
        public uint Major {
            get { return major; }
        }
        
        public uint Minor {
            get { return minor; }
        }
        
        public uint Micro {
            get { return micro; }
        }
        
        public uint Nano {
            get { return nano; }
        }
        
        [DllImport("gstreamer-0.10")]
        private static extern void gst_version(out uint major, out uint minor, out uint micro, out uint nano);
        
        [DllImport("gstreamer-0.10")]
        private static extern IntPtr gst_version_string();
    }
}