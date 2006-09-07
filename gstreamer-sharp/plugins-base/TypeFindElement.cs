//
// TypeFindElement.cs: typefind element binding
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
//
// (C) 2006 Novell, Inc.
//

using System;

namespace Gst
{
    public delegate void HaveTypeHandler(object o, HaveTypeArgs args);

    public class HaveTypeArgs : GLib.SignalArgs 
    {
        public HaveTypeArgs(GLib.SignalArgs args) : base(args)
        {
        }
    
        public uint Probability {
            get { return (uint)Args[0]; }
        }
        
        public Gst.Caps Caps {
            get { 
				return (Gst.Caps)Args[1]; 
			}
        }
    }

    public class TypeFindElement : Element 
    {
        private Delegate have_type_delegate;
    
        public TypeFindElement(IntPtr raw) : base(raw) 
        {
        } 
        
        public static TypeFindElement Make(string name)
        {
            return ElementFactory.Make("typefind", name) as TypeFindElement;
        }
    /* 
        protected virtual void OnHaveType(object o, GLib.SignalArgs args)
        {
            BindingHelper.InvokeProxySignalDelegate(have_type_delegate, typeof(HaveTypeArgs), o, args);
        }
	*/

     
        public event GLib.DynamicSignalHandler HaveType {
            add {
				/*
                have_type_delegate = BindingHelper.AddProxySignalDelegate(this, "have-type", 
                    OnHaveType, have_type_delegate, value);
				*/
				GLib.DynamicSignal.Connect(this, "have-type", value);
            }
            
            remove {
				/*
                have_type_delegate = BindingHelper.RemoveProxySignalDelegate(this, "have-type", 
                    OnHaveType, have_type_delegate, value);
				*/
				GLib.DynamicSignal.Disconnect(this, "have-type", value);
            }
        }
        
        [GLib.Property("caps")]
        public Gst.Caps Caps {
            get { 
                GLib.Value val = GetProperty("caps");
                Gst.Caps caps = (Gst.Caps)val.Val;
                val.Dispose();
                return caps;
            }
        }
        
        [GLib.Property("minimum")]
        public uint Minimum {
            get {
                GLib.Value val = GetProperty("minimum");
                uint ret = (uint)val.Val;
                val.Dispose();
                return ret;
            }

            set {
                GLib.Value val = new GLib.Value(value);
                SetProperty("minimum", val);
                val.Dispose();
            }
        }
        
        [GLib.Property("maximum")]
        public uint Maximum {
            get {
                GLib.Value val = GetProperty("maximum");
                uint ret = (uint)val.Val;
                val.Dispose();
                return ret;
            }

            set {
                GLib.Value val = new GLib.Value(value);
                SetProperty("maximum", val);
                val.Dispose();
            }
        }
    }
}
