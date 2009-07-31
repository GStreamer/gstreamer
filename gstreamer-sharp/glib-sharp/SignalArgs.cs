// GLib.SignalArgs.cs - Signal argument class implementation
//
// Author: Mike Kestner <mkestner@speakeasy.net>
//
// Copyright (c) 2001 Mike Kestner
// Copyright (c) 2004 Novell, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of version 2 of the Lesser GNU General 
// Public License as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this program; if not, write to the
// Free Software Foundation, Inc., 59 Temple Place - Suite 330,
// Boston, MA 02111-1307, USA.


namespace GLib {
	using System;
	using System.Collections;

	public class SignalArgs : EventArgs {

		private object _ret;
		private object[] _args;

		public SignalArgs()
		{
			_ret = null;
			_args = null;
		}

		public SignalArgs(object retval)
		{
			_ret = retval;
			_args = null;
		}

		public SignalArgs(object retval, object[] args)
		{
			_ret = retval;
			_args = args;
		}

		public object[] Args {
			get {
				return _args;
			}
			set {
				_args = value;
			}
		}

		public object RetVal {
			get {
				return _ret;
			}
			set {
				_ret = value;
			}
		}
	}
}
