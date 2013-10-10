// Copyright (C) 2013  Stephan Sundermann <stephansundermann@gmail.com>
// 
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU Affero General Public License as
// published by the Free Software Foundation, either version 3 of the
// License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Affero General Public License for more details.
//
// You should have received a copy of the GNU Affero General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.

using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

namespace Gst {

	public class PropertyNotFoundException : Exception {}
	
	partial class Object 
	{
		private Dictionary <string, bool> PropertyNameCache = new Dictionary<string, bool> ();

		[DllImport ("libgobject-2.0-0.dll", CallingConvention = CallingConvention.Cdecl)]
		static extern IntPtr g_object_class_find_property (IntPtr klass, IntPtr name);

		bool PropertyExists (string name) {
			if (PropertyNameCache.ContainsKey (name))
				return PropertyNameCache [name];

			var ptr = g_object_class_find_property (Marshal.ReadIntPtr (Handle), GLib.Marshaller.StringToPtrGStrdup (name));
			var result = ptr != IntPtr.Zero;

			// just cache the positive results because there might
			// actually be new properties getting installed
			if (result)
				PropertyNameCache [name] = result;

			return result;
		}

		public object this[string property] {
		  get {
				if (PropertyExists (property)) {
					using (GLib.Value v = GetProperty (property)) {
						return v.Val;
					}
				} else
					throw new PropertyNotFoundException ();
		  } set {
				if (PropertyExists (property)) {
					using (GLib.Value v = new GLib.Value (this, property)) {
						v.Val = value;
						SetProperty (property, v);
					}
				} else
					throw new PropertyNotFoundException ();
		  }
		}

		public void Connect (string signal, SignalHandler handler) {
		  DynamicSignal.Connect (this, signal, handler);
		}

		public void Disconnect (string signal, SignalHandler handler) {
		  DynamicSignal.Disconnect (this, signal, handler);
		}

		public void Connect (string signal, Delegate handler) {
		  DynamicSignal.Connect (this, signal, handler);
		}

		public void Disconnect (string signal, Delegate handler) {
		  DynamicSignal.Disconnect (this, signal, handler);
		}

		public object Emit (string signal, params object[] parameters) {
		  return DynamicSignal.Emit (this, signal, parameters);
		}
	}
}