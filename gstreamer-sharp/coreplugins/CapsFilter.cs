//
// CapsFilter.cs: capsfilter element bindings
//
// Authors:
//   Maarten Bosmans <mkbosmans@gmail.com>
//

using System;

namespace Gst.CorePlugins
{
    [GTypeName("GstCapsFilter")]
    public class CapsFilter : Element 
    {
        public CapsFilter(IntPtr raw) : base(raw) 
        {
        } 
        
        public static CapsFilter Make(string name)
        {
            return ElementFactory.Make("capsfilter", name) as CapsFilter;
        }

        [GLib.Property("caps")]
        public Gst.Caps Caps {
            get { 
                GLib.Value val = GetProperty("caps");
                Gst.Caps caps = (Gst.Caps)val.Val;
                val.Dispose();
                return caps;
            }
	    set {
                GLib.Value val = new GLib.Value(value);
                SetProperty("caps", val);
                val.Dispose();
	    }
        }
    }
}
