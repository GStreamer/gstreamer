//
// PlayBin.cs: playbin element binding
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
//
// (C) 2006 Novell, Inc.
//

using System;
using System.Collections;
using System.Runtime.InteropServices;

namespace Gst
{
    public class PlayBin : Pipeline 
    {
        public PlayBin(IntPtr raw) : base(raw) 
        {
        }

        [GLib.Property("uri")]
        public string Uri {
            get {
                GLib.Value val = GetProperty("uri");
                string ret = val.Val as string;
                val.Dispose();
                return ret;
            }

            set {
                GLib.Value val = new GLib.Value(value);
                SetProperty("uri", val);
                val.Dispose();
            }
        }
        
        [GLib.Property("suburi")]
        public string SubUri {
            get {
                GLib.Value val = GetProperty("suburi");
                string ret = val.Val as string;
                val.Dispose();
                return ret;
            }

            set {
                GLib.Value val = new GLib.Value(value);
                SetProperty("suburi", val);
                val.Dispose();
            }
        }
        
        [GLib.Property("source")]
        public Element Source {
            get {
                GLib.Value val = GetProperty("source");
                Element element = val.Val as Element;
                val.Dispose();
                return element;
            }
        }
        
        [GLib.Property("audio-sink")]
        public Element AudioSink {
            get {
                GLib.Value val = GetProperty("audio-sink");
                Element ret = val.Val as Element;
                val.Dispose();
                return ret;
            }

            set {
                GLib.Value val = new GLib.Value(value);
                SetProperty("audio-sink", val);
                val.Dispose();
            }
        }
        
        [GLib.Property("video-sink")]
        public Element VideoSink {
            get {
                GLib.Value val = GetProperty("video-sink");
                Element ret = val.Val as Element;
                val.Dispose();
                return ret;
            }

            set {
                GLib.Value val = new GLib.Value(value);
                SetProperty("video-sink", val);
                val.Dispose();
            }
        }
        
        [GLib.Property("vis-plugin")]
        public Element VisPlugin {
            get {
                GLib.Value val = GetProperty("vis-plugin");
                Element ret = val.Val as Element;
                val.Dispose();
                return ret;
            }

            set {
                GLib.Value val = new GLib.Value(value);
                SetProperty("vis-plugin", val);
                val.Dispose();
            }
        }
         
        [GLib.Property("volume")]
        public double Volume {
            get {
                GLib.Value val = GetProperty("volume");
                double ret = (double)val.Val;
                val.Dispose();
                return ret;
            }

            set {
                GLib.Value val = new GLib.Value(value);
                SetProperty("volume", val);
                val.Dispose();
            }
        }
    }
}
