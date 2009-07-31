// TypeInitializerAttribute.cs
//
// Author:   Mike Kestner  <mkestner@novell.com>
//
// Copyright (c) 2007 Novell, Inc.
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

	[AttributeUsage (AttributeTargets.Class)]
	public sealed class TypeInitializerAttribute : Attribute 
	{
		string method_name;
		Type type;

		public TypeInitializerAttribute (Type type, string method_name) 
		{
			this.type = type;
			this.method_name = method_name;
		}

		public string MethodName {
			get { return method_name; }
			set { method_name = value; }
		}

		public Type Type {
			get { return type; }
			set { type = value; }
		}
	}
}
