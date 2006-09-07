//
//
// (C) 2006 Novell Inc.
//
// This class implements the functionalities to bind a callback
// function to a signal dynamically.  
// 
//

using GLib;
using System;
using System.Runtime.InteropServices;
using System.Collections;

namespace GLib {
	
	public delegate void DynamicSignalHandler(object o, SignalArgs args);

	delegate void GClosureMarshal (IntPtr closure, ref GLib.Value retval, uint argc, IntPtr argsPtr, 
		IntPtr invocation_hint, IntPtr data);

	public class DynamicSignal {
		
		private static readonly int gvalue_struct_size = Marshal.SizeOf(typeof(GLib.Value));

		class Key {
			object o;
			string signal_name;

			public Key (object o, string name) {
				this.o = o;
				signal_name = name;
			}

			public override bool Equals(object o) {
				if(o is Key) {
					Key k = (Key) o;
					return k.o.Equals(this.o) && signal_name.Equals(k.signal_name);
				}
				return base.Equals(o);
			}

			public override int GetHashCode() {
				return o.GetHashCode() ^ signal_name.GetHashCode();
			}
		}

		class SignalInfo {
			uint handlerId;
			IntPtr closure;
			Delegate registeredHandler;

			public IntPtr Closure {
				get { return closure; }
				set { closure = value; }
			}

			public uint HandlerId {
				get { return handlerId; }
				set { handlerId = value; }			
			}

			public Delegate RegisteredHandler {
				get { return registeredHandler; }
				set { registeredHandler = value; }
			}

			public SignalInfo(uint handlerId, IntPtr closure, Delegate registeredHandler) {
				this.handlerId = handlerId;
				this.closure = closure;
				this.registeredHandler = registeredHandler;
			}
		}

		static Hashtable SignalHandlers = new Hashtable();

		static GClosureMarshal marshalHandler = new GClosureMarshal(OnMarshal);

		public static void Connect(GLib.Object o, string name, DynamicSignalHandler handler) {
			Connect(o, name, false, handler);
		}

		public static void Connect(GLib.Object o, string name, 
			bool after, DynamicSignalHandler handler)
		{
			Delegate newHandler;

			Key k = new Key(o, name);
						
			if(SignalHandlers[k] != null) {
				SignalInfo si = (SignalInfo) SignalHandlers[k];
				newHandler = Delegate.Combine(si.RegisteredHandler, handler);
				si.RegisteredHandler = newHandler;
			}
			else {
				IntPtr closure = g_closure_new_simple(16, IntPtr.Zero);
				g_closure_set_meta_marshal(closure, (IntPtr) GCHandle.Alloc(k),  marshalHandler);
				uint signalId = g_signal_connect_closure(o.Handle, name, closure, after); 
				SignalHandlers.Add(k, new SignalInfo(signalId, closure, handler));
			}
		}


		public static void Disconnect(GLib.Object o, string name, DynamicSignalHandler handler) {
			Key k = new Key(o, name);
			if(SignalHandlers[k] != null) 
			{
				SignalInfo si = (SignalInfo) SignalHandlers[k];
				Delegate newHandler = Delegate.Remove(si.RegisteredHandler, handler);
				if(newHandler == null || handler == null) {
					g_signal_handler_disconnect(o.Handle, si.HandlerId);
					SignalHandlers.Remove(k);
				} else {
					si.RegisteredHandler = newHandler;
				}
			}
		}

		[DllImport("libgobject-2.0-0.dll")]
		static extern IntPtr g_value_peek_pointer(IntPtr ptr);

		static void OnMarshal(IntPtr closure, ref GLib.Value retval, uint argc, IntPtr argsPtr, 
			IntPtr ihint, IntPtr data) 
		{
			object [] args = new object[argc - 1];
			object o = ((GLib.Value) Marshal.PtrToStructure(argsPtr, typeof(GLib.Value))).Val;

			for(int i=1; i < argc; i++) {
				IntPtr struct_ptr = (IntPtr)((long) argsPtr + (i * gvalue_struct_size));
				Type detectedType = GLib.GType.LookupType(Marshal.ReadIntPtr(struct_ptr));
				if(detectedType.IsSubclassOf(typeof(Opaque))) {
					args[i - 1] = (Opaque) Opaque.GetOpaque(g_value_peek_pointer(struct_ptr), detectedType, false);
				}
				else {
					GLib.Value argument = (GLib.Value) Marshal.PtrToStructure(struct_ptr, typeof(GLib.Value));
					args[i - 1] = argument.Val;
				}
			}

			if(data == IntPtr.Zero) {
				Console.Error.WriteLine("No available data");
			}

			Key k = (Key)((GCHandle) data).Target;
			if(k != null) {
				SignalArgs arg = new SignalArgs();
				arg.Args = args;
				SignalInfo si = (SignalInfo) SignalHandlers[k];
				DynamicSignalHandler handler = (DynamicSignalHandler) si.RegisteredHandler;
				handler(o, arg);
				if(arg.RetVal != null) {
					retval.Val = arg.RetVal;
				}
			}
		}

		
		[DllImport("gobject-2.0.dll")]
		static extern IntPtr g_closure_new_simple(int size, IntPtr data);

		[DllImport("gobject-2.0.dll")]
		static extern uint g_signal_connect_closure(IntPtr instance, 
			string name, IntPtr closure, bool after);

		[DllImport("gobject-2.0.dll")]
		static extern void g_closure_set_meta_marshal(IntPtr closure, IntPtr data, GClosureMarshal marshal);

		[DllImport("gobject-2.0.dll")]
		static extern int g_signal_handler_disconnect(IntPtr o, uint handler_id);
	}
}
