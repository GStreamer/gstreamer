//
// DynamicSignal.cs: C# bindings to g_dynamic_signal to provide
//   dynamic runtime signal binding for GObject 
//
// Authors:
//   Aaron Bockover (abockover@novell.com)
//
// (C) 2006 Novell, Inc.
//

using System;
using System.Runtime.InteropServices;

namespace GLib
{    
    public delegate void DynamicSignalHandler(object o, DynamicSignalArgs args);
        
    public class DynamicSignalArgs : EventArgs
    {
        private GLib.Object sender;
        private object [] args;

        public DynamicSignalArgs()
        {
        }
        
        public DynamicSignalArgs(DynamicSignalArgs args)
        {
            Sender = args.Sender;
            Args = args.Args;
        }

        public object this[int index] {
            get { return Args[index]; }
        }

        public GLib.Object Sender {
            get { return sender; }
            internal set { sender = value; }
        }
        
        public object [] Args { 
            get { return args; }
            internal set { args = value; }
        }
    }

    public static class DynamicSignal
    {
        private delegate void DynamicSignalNativeHandler(IntPtr objectPtr, uint argc, 
            IntPtr argsPtr, IntPtr userdata);
            
        private static DynamicSignalNativeHandler dynamic_signal_handler = 
            new DynamicSignalNativeHandler(OnDynamicSignalRaised);
            
        private static readonly int gvalue_struct_size = Marshal.SizeOf(typeof(GLib.Value));
        
        public static void Connect(GLib.Object o, string name, DynamicSignalHandler handler)
        {
            Connect(o, name, false, handler);
        }
        
        public static void Connect(GLib.Object o, string name, bool after, 
            DynamicSignalHandler handler)
        {
            IntPtr entry_ptr = FindRegisteredEntry(o, name);
            if(entry_ptr == IntPtr.Zero) {
                g_dynamic_signal_connect(o.Handle, name, dynamic_signal_handler, 
                    after, (IntPtr)GCHandle.Alloc(handler));
                return;
            }
            
            Delegate registered_handler = GetEntryHandler(entry_ptr);
            if(registered_handler != null) {
                Delegate new_handler = Delegate.Combine(registered_handler, handler);
                if(new_handler != registered_handler) {
                    g_dynamic_signal_update_entry_userdata(entry_ptr, 
                        (IntPtr)GCHandle.Alloc(new_handler));
                }
            }
        }
        
        public static void Disconnect(GLib.Object o, string name, DynamicSignalHandler handler)
        {
            IntPtr entry_ptr = FindRegisteredEntry(o, name);
            if(entry_ptr == IntPtr.Zero) {
                return;
            }
            
            Delegate registered_handler = GetEntryHandler(entry_ptr);
            Delegate new_handler = Delegate.Remove(registered_handler, handler);
            if(new_handler == null || handler == null) {
                g_dynamic_signal_disconnect(o.Handle, name);
                return;
            } else if(new_handler != null && registered_handler != new_handler) {
                g_dynamic_signal_update_entry_userdata(entry_ptr, 
                    (IntPtr)GCHandle.Alloc(new_handler));
            }   
        }
        
        private static IntPtr FindRegisteredEntry(GLib.Object o, string name)
        {
            return g_dynamic_signal_find_registration(o.Handle, name);
        }
        
        private static DynamicSignalHandler GetEntryHandler(IntPtr entryPtr)
        {
            IntPtr handler_ptr = Marshal.ReadIntPtr(entryPtr, Marshal.SizeOf(typeof(IntPtr)));
            return (DynamicSignalHandler)((GCHandle)handler_ptr).Target;
        }
        
        private static void OnDynamicSignalRaised(IntPtr objectPtr, uint argc, 
            IntPtr argsPtr, IntPtr userdata)
        {
            GLib.Object gobject = GLib.Object.GetObject(objectPtr, false);
            object [] args = new object[argc];
                
            for(int i = 0; i < argc; i++) {
                IntPtr struct_ptr = (IntPtr)((long)argsPtr + (i * gvalue_struct_size));
                GLib.Value argument = (GLib.Value)Marshal.PtrToStructure(
                    struct_ptr, typeof(GLib.Value));
                Type type = GType.LookupType(g_value_type(struct_ptr));
                
                if(type.IsSubclassOf(typeof(GLib.Opaque))) {
                    args[i] = GLib.Opaque.GetOpaque(g_value_peek_pointer(struct_ptr), type, true);
                } else {
                    args[i] = argument.Val;
                }
            }
            
            DynamicSignalHandler handler = (DynamicSignalHandler)((GCHandle)userdata).Target;
            if(handler != null) {
                DynamicSignalArgs dargs = new DynamicSignalArgs();
                dargs.Sender = gobject;
                dargs.Args = args;
                handler(gobject, dargs);
            }
        }

        [DllImport("gstreamersharpglue-0.10")]
        private static extern uint g_dynamic_signal_connect(IntPtr o, string name, 
            DynamicSignalNativeHandler callback, bool swapped, IntPtr userdata);
            
        [DllImport("gstreamersharpglue-0.10")]
        private static extern void g_dynamic_signal_disconnect(IntPtr o, string name);
        
        [DllImport("gstreamersharpglue-0.10")]
        private static extern IntPtr g_dynamic_signal_find_registration(IntPtr o, string name);
        
        [DllImport("gstreamersharpglue-0.10")]
        private static extern void g_dynamic_signal_update_entry_userdata(IntPtr entry, IntPtr userdata);

        [DllImport("gstreamersharpglue-0.10")]
        private static extern IntPtr g_value_type(IntPtr value);

        [DllImport("libgobject-2.0-0.dll")]
        private static extern IntPtr g_value_peek_pointer(IntPtr value);
    }
}

