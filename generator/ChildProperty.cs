// GtkSharp.Generation.ChildProperty.cs - GtkContainer child properties
//
// Copyright (c) 2004 Novell, Inc.
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of version 2 of the GNU General Public
// License as published by the Free Software Foundation.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// General Public License for more details.
//
// You should have received a copy of the GNU General Public
// License along with this program; if not, write to the
// Free Software Foundation, Inc., 59 Temple Place - Suite 330,
// Boston, MA 02111-1307, USA.


namespace GtkSharp.Generation {

	using System;
	using System.Collections;
	using System.IO;
	using System.Xml;

	public class ChildProperty : Property {

		public ChildProperty (XmlElement elem, ClassBase container_type) : base (elem, container_type) {}

		protected override string PropertyAttribute (string qpname) {
			return "[Gtk.ChildProperty (" + qpname + ")]";
		}

		protected override string RawGetter (string qpname) {
			return "parent.ChildGetProperty (child, " + qpname + ")";
		}

		protected override string RawSetter (string qpname) {
			return "parent.ChildSetProperty(child, " + qpname + ", val)";
		}

	}
}

