// GtkSharp.Generation.MethodBase.cs - function element base class.
//
// Author: Mike Kestner <mkestner@novell.com>
//
// Copyright (c) 2001-2003 Mike Kestner
// Copyright (c) 2004-2005 Novell, Inc.
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

	public abstract class MethodBase  {

		protected XmlElement elem;
		protected ClassBase container_type;
		protected Parameters parms;
		string mods = String.Empty;
		string name;
		private string protection = "public";

		protected MethodBase (XmlElement elem, ClassBase container_type) 
		{
			this.elem = elem;
			this.container_type = container_type;
			this.name = elem.GetAttribute ("name");
			parms = new Parameters (elem ["parameters"]);
			IsStatic = elem.GetAttribute ("shared") == "true";
			if (elem.HasAttribute ("new_flag"))
				mods = "new ";
			if (elem.HasAttribute ("accessibility")) {
				string attr = elem.GetAttribute ("accessibility");
				switch (attr) {
					case "public":
					case "protected":
					case "internal":
					case "private":
					case "protected internal":
						protection = attr;
						break;
				}
			}
		}

		protected string BaseName {
			get {
				string name = Name;
				int idx = Name.LastIndexOf (".");
				if (idx > 0)
					name = Name.Substring (idx + 1);
				return name;
			}
		}

		MethodBody body;
		public MethodBody Body {
			get {
				if (body == null)
					body = new MethodBody (parms);
				return body;
			}
		}

		public virtual string CName {
			get {
				return SymbolTable.Table.MangleName (elem.GetAttribute ("cname"));
			}
		}

		protected bool HasGetterName {
			get {
				string name = BaseName;
				if (name.Length <= 3)
					return false;
				if (name.StartsWith ("Get") || name.StartsWith ("Has"))
					return Char.IsUpper (name [3]);
				else if (name.StartsWith ("Is"))
					return Char.IsUpper (name [2]);
				else
					return false;
			}
		}

		protected bool HasSetterName {
			get {
				string name = BaseName;
				if (name.Length <= 3)
					return false;

				return name.StartsWith ("Set") && Char.IsUpper (name [3]);
			}
		}

		public bool IsStatic {
			get {
				return parms.Static;
			}
			set {
				parms.Static = value;
			}
		}

		public string LibraryName {
			get {
				if (elem.HasAttribute ("library"))
					return elem.GetAttribute ("library");
				return container_type.LibraryName;
			}
		}

		public string Modifiers {
			get {
				return mods;
			}
			set {
				mods = value;
			}
		}

		public string Name {
			get {
				return name;
			}
			set {
				name = value;
			}
		}

		public Parameters Parameters {
			get {
				return parms;
			}
		}

	
		public string Protection {
			get { return protection; }
			set { protection = value; }
		}

		protected string Safety {
			get {
				return Body.ThrowsException && !(container_type is InterfaceGen) ? "unsafe " : "";
			}
		}

		Signature sig;
		public Signature Signature {
			get {
				if (sig == null)
					sig = new Signature (parms);
				return sig;
			}
		}

		public virtual bool Validate ()
		{
			if (!parms.Validate ()) {
				Console.Write("in " + CName + " ");
				Statistics.ThrottledCount++;
				return false;
			}

			return true;
		}
	}
}

