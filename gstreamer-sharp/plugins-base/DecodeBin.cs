//
// DecodeBin.cs: decodebin element binding
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
    public delegate void NewDecodedPadHandler(object o, NewDecodedPadArgs args);

    public class NewDecodedPadArgs : GLib.SignalArgs 
    {
        public Gst.Pad Pad {
            get { return (Gst.Pad)Args[0]; }
        }
        
        public bool Last {
            get { return (bool)Args[1]; }
        }
    }

    public class DecodeBin : Bin 
    {
        public DecodeBin(IntPtr raw) : base(raw) 
        {
        }
        
        [GLib.CDeclCallback]
        private delegate void NewDecodedPadSignalDelegate(IntPtr arg0, IntPtr arg1, bool arg2, IntPtr gch);

        private static void NewDecodedPadSignalCallback(IntPtr arg0, IntPtr arg1, bool arg2, IntPtr gch)
        {
            GLib.Signal sig = ((GCHandle)gch).Target as GLib.Signal;
            if(sig == null) {
                throw new Exception("Unknown signal GC handle received " + gch);
            }
            
            Gst.NewDecodedPadArgs args = new Gst.NewDecodedPadArgs();
            args.Args = new object[2];
            args.Args[0] = GLib.Object.GetObject(arg1) as Gst.Pad;
            args.Args[1] = arg2;
            
            Gst.NewDecodedPadHandler handler = (Gst.NewDecodedPadHandler)sig.Handler;
            handler(GLib.Object.GetObject(arg0), args);
        }

        [GLib.CDeclCallback]
        private delegate void NewDecodedPadVMDelegate(IntPtr bin, IntPtr pad, bool last);

        private static NewDecodedPadVMDelegate NewDecodedPadVMCallback;

        private static void newdecodedpad_cb(IntPtr bin, IntPtr pad, bool last)
        {
            DecodeBin bin_managed = GLib.Object.GetObject(bin, false) as DecodeBin;
            bin_managed.OnNewDecodedPad(GLib.Object.GetObject(pad) as Gst.Pad, last);
        }

        private static void OverrideNewDecodedPad(GLib.GType gtype)
        {
            if(NewDecodedPadVMCallback == null) {
                NewDecodedPadVMCallback = new NewDecodedPadVMDelegate(newdecodedpad_cb);
            }
            
            OverrideVirtualMethod(gtype, "new-decoded-pad", NewDecodedPadVMCallback);
        }

        [GLib.DefaultSignalHandler(Type=typeof(Gst.DecodeBin), ConnectionMethod="OverrideNewDecodedPad")]
        protected virtual void OnNewDecodedPad(Gst.Pad pad, bool last)
        {
            GLib.Value ret = GLib.Value.Empty;
            GLib.ValueArray inst_and_params = new GLib.ValueArray(3);
            GLib.Value [] vals = new GLib.Value[3];
            
            vals[0] = new GLib.Value(this);
            inst_and_params.Append(vals[0]);
            
            vals[1] = new GLib.Value(pad);
            inst_and_params.Append(vals[1]);
            
            vals[2] = new GLib.Value(last);
            inst_and_params.Append(vals[2]);
            
            g_signal_chain_from_overridden(inst_and_params.ArrayPtr, ref ret);
            
            foreach(GLib.Value v in vals) {
                v.Dispose();
            }
        }

        [GLib.Signal("new-decoded-pad")]
        public event Gst.NewDecodedPadHandler NewDecodedPad {
            add {
                GLib.Signal sig = GLib.Signal.Lookup(this, "new-decoded-pad", 
                    new NewDecodedPadSignalDelegate(NewDecodedPadSignalCallback));
                sig.AddDelegate(value);
            }
            
            remove {
                GLib.Signal sig = GLib.Signal.Lookup(this, "new-decoded-pad", 
                    new NewDecodedPadSignalDelegate(NewDecodedPadSignalCallback));
                sig.RemoveDelegate(value);
            }
        }
    }
}
