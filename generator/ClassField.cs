// GtkSharp.Generation.ClassField.cs - used in class structures
//
// Copyright (c) 2009 Christian Hoff
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

	public class ClassField : StructField {
		protected new ObjectBase container_type;

		public ClassField (XmlElement elem, ObjectBase container_type) : base (elem, container_type) {
			this.container_type = container_type;
		}

		public override bool Validate () {
			if (IsBitfield) {
				Console.WriteLine ("Field {0}.{1} is a bitfield which is not supported yet", container_type.ClassStructName, Name);
				return false;
			}

			return base.Validate ();
		}
	}
}
