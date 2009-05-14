// GtkSharp.Generation.PropertyBase.cs - base class for properties and
// fields
//
// Copyright (c) 2005 Novell, Inc.
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
	using System.Xml;

	public abstract class PropertyBase {

		protected XmlElement elem;
		protected ClassBase container_type;

		public PropertyBase (XmlElement elem, ClassBase container_type)
		{
			this.elem = elem;
			this.container_type = container_type;
		}

		public string Name {
			get {
				return elem.GetAttribute ("name");
			}
		}

		public string CName {
			get {
				return elem.GetAttribute ("cname");
			}
		}

		protected string ctype;
		public string CType {
			get {
				if (ctype == null) {
					if (elem.GetAttribute("bits") == "1")
						ctype = "gboolean";
					else
						ctype = elem.GetAttribute("type");
				}
				return ctype;
			}
		}

		protected string cstype;
		public string CSType {
			get {
				if (Getter != null)
					return Getter.Signature.IsAccessor ? Getter.Signature.AccessorType : Getter.ReturnType;
				else if (Setter != null)
					return Setter.Signature.Types;
				else if (cstype == null)
					cstype = SymbolTable.Table.GetCSType (CType);
				return cstype;
			}
		}

		public bool Hidden {
			get {
				return elem.HasAttribute("hidden");
			}
		}

		protected bool IsNew {
			get {
				return elem.HasAttribute("new_flag");
			}
		}

		protected Method Getter {
			get {
				Method getter = container_type.GetMethod ("Get" + Name);
				if (getter != null && getter.Name == "Get" + Name && getter.IsGetter)
					return getter;
				else
					return null;
			}
		}

		protected Method Setter {
			get {
				Method setter = container_type.GetMethod ("Set" + Name);
				if (setter != null && setter.Name == "Set" + Name && setter.IsSetter && (Getter == null || setter.Signature.Types == CSType))
					return setter;
				else
					return null;
			}
		}

		protected virtual void GenerateImports (GenerationInfo gen_info, string indent)
		{
			if (Getter != null)
				Getter.GenerateImport (gen_info.Writer);
			if (Setter != null)
				Setter.GenerateImport (gen_info.Writer);
		}
	}
}

