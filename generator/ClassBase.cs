// GtkSharp.Generation.ClassBase.cs - Common code between object
// and interface wrappers
//
// Authors: Rachel Hestilow <hestilow@ximian.com>
//          Mike Kestner <mkestner@speakeasy.net>
//
// Copyright (c) 2002 Rachel Hestilow
// Copyright (c) 2001-2003 Mike Kestner 
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

	public abstract class ClassBase : GenBase {
		protected Hashtable props = new Hashtable();
		protected Hashtable fields = new Hashtable();
		protected Hashtable methods = new Hashtable();
		protected ArrayList interfaces = new ArrayList();
		protected ArrayList managed_interfaces = new ArrayList();
		protected ArrayList ctors = new ArrayList();

		private bool ctors_initted = false;
		private Hashtable clash_map;
		private bool deprecated = false;
		private bool isabstract = false;

		public Hashtable Methods {
			get {
				return methods;
			}
		}	

		public ClassBase Parent {
			get {
				string parent = Elem.GetAttribute("parent");

				if (parent == "")
					return null;
				else
					return SymbolTable.Table.GetClassGen(parent);
			}
		}

		protected ClassBase (XmlElement ns, XmlElement elem) : base (ns, elem) {
					
			if (elem.HasAttribute ("deprecated")) {
				string attr = elem.GetAttribute ("deprecated");
				deprecated = attr == "1" || attr == "true";
			}
			
			if (elem.HasAttribute ("abstract")) {
				string attr = elem.GetAttribute ("abstract");
				isabstract = attr == "1" || attr == "true";
			}

			foreach (XmlNode node in elem.ChildNodes) {
				if (!(node is XmlElement)) continue;
				XmlElement member = (XmlElement) node;
				if (member.HasAttribute ("hidden"))
					continue;
				
				string name;
				switch (node.Name) {
				case "method":
					name = member.GetAttribute("name");
					while (methods.ContainsKey(name))
						name += "mangled";
					methods.Add (name, new Method (member, this));
					break;

				case "property":
					name = member.GetAttribute("name");
					while (props.ContainsKey(name))
						name += "mangled";
					props.Add (name, new Property (member, this));
					break;

				case "field":
					name = member.GetAttribute("name");
					while (fields.ContainsKey (name))
						name += "mangled";
					fields.Add (name, new ObjectField (member, this));
					break;

				case "implements":
					ParseImplements (member);
					break;

				case "constructor":
					ctors.Add (new Ctor (member, this));
					break;

				default:
					break;
				}
			}
		}

		public override bool Validate ()
		{
			foreach (string iface in interfaces) {
				InterfaceGen igen = SymbolTable.Table[iface] as InterfaceGen;
				if (igen == null) {
					Console.WriteLine (QualifiedName + " implements unknown GInterface " + iface);
					return false;
				}
				if (!igen.ValidateForSubclass ()) {
					Console.WriteLine (QualifiedName + " implements invalid GInterface " + iface);
					return false;
				}
			}

			ArrayList invalids = new ArrayList ();

			foreach (Property prop in props.Values) {
				if (!prop.Validate ()) {
					Console.WriteLine ("in type " + QualifiedName);
					invalids.Add (prop);
				}
			}
			foreach (Property prop in invalids)
				props.Remove (prop.Name);
			invalids.Clear ();

			foreach (ObjectField field in fields.Values) {
				if (!field.Validate ()) {
					Console.WriteLine ("in type " + QualifiedName);
					invalids.Add (field);
				}
			}
			foreach (ObjectField field in invalids)
				fields.Remove (field.Name);
			invalids.Clear ();

			foreach (Method method in methods.Values) {
				if (!method.Validate ()) {
					Console.WriteLine ("in type " + QualifiedName);
					invalids.Add (method);
				}
			}
			foreach (Method method in invalids)
				methods.Remove (method.Name);
			invalids.Clear ();

			foreach (Ctor ctor in ctors) {
				if (!ctor.Validate ()) {
					Console.WriteLine ("in type " + QualifiedName);
					invalids.Add (ctor);
				}
			}
			foreach (Ctor ctor in invalids)
				ctors.Remove (ctor);
			invalids.Clear ();

			return true;
		}

		public bool IsDeprecated {
			get {
				return deprecated;
			}
		}

		public bool IsAbstract {
			get {
				return isabstract;
			}
		}

		public abstract string AssignToName { get; }

		public abstract string CallByName ();

		public override string DefaultValue {
			get {
				return "null";
			}
		}

		protected virtual bool IsNodeNameHandled (string name)
		{
			switch (name) {
			case "method":
			case "property":
			case "field":
			case "signal":
			case "implements":
			case "constructor":
			case "disabledefaultconstructor":
				return true;
				
			default:
				return false;
			}
		}

		public void GenProperties (GenerationInfo gen_info, ClassBase implementor)
		{		
			if (props.Count == 0)
				return;

			foreach (Property prop in props.Values)
				prop.Generate (gen_info, "\t\t", implementor);
		}

		protected void GenFields (GenerationInfo gen_info)
		{
			foreach (ObjectField field in fields.Values)
				field.Generate (gen_info, "\t\t");
		}

		private void ParseImplements (XmlElement member)
		{
			foreach (XmlNode node in member.ChildNodes) {
				if (node.Name != "interface")
					continue;
				XmlElement element = (XmlElement) node;
				if (element.HasAttribute ("hidden"))
					continue;
				if (element.HasAttribute ("cname"))
					interfaces.Add (element.GetAttribute ("cname"));
				else if (element.HasAttribute ("name"))
					managed_interfaces.Add (element.GetAttribute ("name"));
			}
		}
		
		protected bool IgnoreMethod (Method method, ClassBase implementor)
		{	
			if (implementor != null && implementor.QualifiedName != this.QualifiedName && method.IsStatic)
				return true;

			string mname = method.Name;
			return ((method.IsSetter || (method.IsGetter && mname.StartsWith("Get"))) &&
				((props != null) && props.ContainsKey(mname.Substring(3)) ||
				 (fields != null) && fields.ContainsKey(mname.Substring(3))));
		}

		public void GenMethods (GenerationInfo gen_info, Hashtable collisions, ClassBase implementor)
		{		
			if (methods == null)
				return;

			foreach (Method method in methods.Values) {
				if (IgnoreMethod (method, implementor))
				    	continue;

				string oname = null, oprotection = null;
				if (collisions != null && collisions.Contains (method.Name)) {
					oname = method.Name;
					oprotection = method.Protection;
					method.Name = QualifiedName + "." + method.Name;
					method.Protection = "";
				}
				method.Generate (gen_info, implementor);
				if (oname != null) {
					method.Name = oname;
					method.Protection = oprotection;
				}
			}
		}

		public Method GetMethod (string name)
		{
			return (Method) methods[name];
		}

		public Property GetProperty (string name)
		{
			return (Property) props[name];
		}

		public Method GetMethodRecursively (string name)
		{
			return GetMethodRecursively (name, false);
		}
		
		public virtual Method GetMethodRecursively (string name, bool check_self)
		{
			Method p = null;
			if (check_self)
				p = GetMethod (name);
			if (p == null && Parent != null) 
				p = Parent.GetMethodRecursively (name, true);
			
			if (check_self && p == null) {
				foreach (string iface in interfaces) {
					ClassBase igen = SymbolTable.Table.GetClassGen (iface);
					if (igen == null)
						continue;
					p = igen.GetMethodRecursively (name, true);
					if (p != null)
						break;
				}
			}

			return p;
		}

		public virtual Property GetPropertyRecursively (string name)
		{
			ClassBase klass = this;
			Property p = null;
			while (klass != null && p == null) {
				p = (Property) klass.GetProperty (name);
				klass = klass.Parent;
			}

			return p;
		}

		public bool Implements (string iface)
		{
			if (interfaces.Contains (iface))
				return true;
			else if (Parent != null)
				return Parent.Implements (iface);
			else
				return false;
		}

		public ArrayList Ctors { get { return ctors; } }

		bool HasStaticCtor (string name) 
		{
			if (Parent != null && Parent.HasStaticCtor (name))
				return true;

			foreach (Ctor ctor in Ctors)
				if (ctor.StaticName == name)
					return true;

			return false;
		}

		private void InitializeCtors ()
		{
			if (ctors_initted)
				return;

			if (Parent != null)
				Parent.InitializeCtors ();

			ArrayList valid_ctors = new ArrayList();
			clash_map = new Hashtable();

			foreach (Ctor ctor in ctors) {
				if (clash_map.Contains (ctor.Signature.Types)) {
					Ctor clash = clash_map [ctor.Signature.Types] as Ctor;
					Ctor alter = ctor.Preferred ? clash : ctor;
					alter.IsStatic = true;
					if (Parent != null && Parent.HasStaticCtor (alter.StaticName))
						alter.Modifiers = "new ";
				} else
					clash_map [ctor.Signature.Types] = ctor;

				valid_ctors.Add (ctor);
			}

			ctors = valid_ctors;
			ctors_initted = true;
		}

		protected virtual void GenCtors (GenerationInfo gen_info)
		{
			InitializeCtors ();
			foreach (Ctor ctor in ctors)
				ctor.Generate (gen_info);
		}

		public virtual void Finish (StreamWriter sw, string indent)
		{
		}

		public virtual void Prepare (StreamWriter sw, string indent)
		{
		}
	}
}
