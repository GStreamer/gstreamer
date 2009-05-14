// GtkSharp.Generation.GenBase.cs - The Generatable base class.
//
// Author: Mike Kestner <mkestner@novell.com>
//
// Copyright (c) 2001-2002 Mike Kestner
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
	using System.IO;
	using System.Xml;

	public abstract class GenBase : IGeneratable {
		
		private XmlElement ns;
		private XmlElement elem;

		protected GenBase (XmlElement ns, XmlElement elem)
		{
			this.ns = ns;
			this.elem = elem;
		}

		public string CName {
			get {
				return elem.GetAttribute ("cname");
			}
		}

		public XmlElement Elem {
			get {
				return elem;
			}
		}

		public int ParserVersion {
			get {
				XmlElement root = elem.OwnerDocument.DocumentElement;	
				return root.HasAttribute ("parser_version") ? int.Parse (root.GetAttribute ("parser_version")) : 1;
			}
		}

		public bool IsInternal {
			get {
				if (elem.HasAttribute ("internal")) {
					string attr = elem.GetAttribute ("internal");
					return attr == "1" || attr == "true";
				}
				return false;
			}
		}

		public string LibraryName {
			get {
				return ns.GetAttribute ("library");
			}
		}

		public virtual string MarshalReturnType { 
			get {
				return MarshalType;
			}
		}

		public abstract string MarshalType { get; }

		public string Name {
			get {
				return elem.GetAttribute ("name");
			}
		}

		public string NS {
			get {
				return ns.GetAttribute ("name");
			}
		}

		public abstract string DefaultValue { get; }

		public string QualifiedName {
			get {
				return NS + "." + Name;
			}
		}

		public virtual string ToNativeReturnType { 
			get {
				return MarshalType;
			}
		}

		protected void AppendCustom (StreamWriter sw, string custom_dir)
		{
			char sep = Path.DirectorySeparatorChar;
			string custom = custom_dir + sep + Name + ".custom";
			if (File.Exists(custom)) {
				sw.WriteLine ("#region Customized extensions");
				sw.WriteLine ("#line 1 \"" + Name + ".custom\"");
				FileStream custstream = new FileStream(custom, FileMode.Open, FileAccess.Read);
				StreamReader sr = new StreamReader(custstream);
				sw.WriteLine (sr.ReadToEnd ());
				sw.WriteLine ("#endregion");
				sr.Close ();
			}
		}

		public abstract string CallByName (string var);

		public abstract string FromNative (string var);

		public virtual string FromNativeReturn (string var)
		{
			return FromNative (var);
		}

		public virtual string ToNativeReturn (string var)
		{
			return CallByName (var);
		}

		public abstract bool Validate ();

		public void Generate ()
		{
			GenerationInfo geninfo = new GenerationInfo (ns);
			Generate (geninfo);
		}

		public abstract void Generate (GenerationInfo geninfo);
	}
}

