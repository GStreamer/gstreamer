//
// DecodeBin.cs: decodebin element binding
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
//
// (C) 2006 Novell, Inc.
//

using System;
using Gst;

namespace Gst
{
    public delegate void NewDecodedPadHandler(object o, NewDecodedPadArgs args);

    public class NewDecodedPadArgs : GLib.SignalArgs 
    {
        public NewDecodedPadArgs(GLib.SignalArgs args) : base(args)
        {
        }
    
        public Gst.Pad Pad {
            get { return (Gst.Pad)Args[1]; }
        }
        
        public bool Last {
            get { return (bool)Args[2]; }
        }
    }

    public class DecodeBin : Bin 
    {
        private Delegate new_decoded_pad_delegate;
    
        public DecodeBin(IntPtr raw) : base(raw) 
        {
        } 
        
        protected virtual void OnNewDecodedPad(object o, GLib.SignalArgs args)
        {
            BindingHelper.InvokeProxySignalDelegate(new_decoded_pad_delegate, 
                typeof(NewDecodedPadArgs), o, args);
        }
     
        public event NewDecodedPadHandler NewDecodedPad {
            add {
                new_decoded_pad_delegate = BindingHelper.AddProxySignalDelegate(this, 
                    "new-decoded-pad", OnNewDecodedPad, new_decoded_pad_delegate, value);
            }
            
            remove {
                new_decoded_pad_delegate = BindingHelper.RemoveProxySignalDelegate(this, 
                    "new-decoded-pad", OnNewDecodedPad, new_decoded_pad_delegate, value);
            }
        }
    }
}
