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

namespace Gst {
	using System;
	using System.Runtime.InteropServices;

	partial class Object 
	{
		public object this[string property] {
		  get {
		    GLib.Value v = GetProperty (property);
		    object o = v.Val;
		    v.Dispose ();
		    return o;
		  } set {
		    GLib.Value v = new GLib.Value (this, property);
		    v.Val = value;
		    SetProperty (property, v);
		    v.Dispose ();
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