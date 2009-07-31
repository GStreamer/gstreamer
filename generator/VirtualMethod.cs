// GtkSharp.Generation.VirtualMethod.cs - The VirtualMethod Generatable.
//
// Author: Mike Kestner <mkestner@novell.com>
//
// Copyright (c) 2003-2004 Novell, Inc.
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

	public abstract class VirtualMethod : MethodBase  {
		protected ReturnValue retval;
		protected ManagedCallString call;
		
		protected string modifiers = "";

		public VirtualMethod (XmlElement elem, ObjectBase container_type) : base (elem, container_type)
		{
			if (container_type.ParserVersion == 1) {
				// The old pre 2.14 parser didn't drop the 1st parameter in all <signal> and <virtual_method> elements
				parms = new Parameters (elem ["parameters"], true);
			}
			retval = new ReturnValue (elem ["return-type"]);
		}
	
		protected abstract string CallString {
			get;
		}

		VMSignature signature;
		protected new VMSignature Signature {
			get {
				if (signature == null)
					signature = new VMSignature (parms);

				return signature;
			}
		}

		/* Creates a callback method which invokes the corresponding virtual method
		* @implementor is the class that implements the virtual method(e.g. the class that derives from an interface) or NULL if containing and declaring type are equal
		*/
		public void GenerateCallback (StreamWriter sw, ClassBase implementor)
		{
			if (!Validate ())
				return;

			string native_signature = "";
			if (!IsStatic) {
				native_signature += "IntPtr inst";
				if (parms.Count > 0)
					native_signature += ", ";
			}
			if (parms.Count > 0)
				native_signature += parms.ImportSignature;

			sw.WriteLine ("\t\t[UnmanagedFunctionPointer (CallingConvention.Cdecl)]");
			sw.WriteLine ("\t\tdelegate {0} {1}NativeDelegate ({2});", retval.ToNativeType, this.Name, native_signature);
			sw.WriteLine ();
			sw.WriteLine ("\t\tstatic {0} {1}_cb ({2})", retval.ToNativeType, this.Name, native_signature);
			sw.WriteLine ("\t\t{");
			string unconditional = call.Unconditional ("\t\t\t");
			if (unconditional.Length > 0)
				sw.WriteLine (unconditional);
			sw.WriteLine ("\t\t\ttry {");

			if (!this.IsStatic) {
				string type;
				if (implementor != null)
					type = implementor.QualifiedName;
				else if (this.container_type is InterfaceGen)
					type = this.container_type.Name + "Implementor"; // We are in an interface/adaptor, invoke the method in the implementor class
				else
					type = this.container_type.Name;

				sw.WriteLine ("\t\t\t\t{0} __obj = GLib.Object.GetObject (inst, false) as {0};", type);
			}

			sw.Write (call.Setup ("\t\t\t\t"));
			sw.Write ("\t\t\t\t");
			if (!retval.IsVoid)
				sw.Write (retval.CSType + " __result = ");
			if (!this.IsStatic)
				sw.Write ("__obj.");
			sw.WriteLine (this.CallString + ";");
			sw.Write (call.Finish ("\t\t\t\t"));
			if (!retval.IsVoid)
				sw.WriteLine ("\t\t\t\treturn " + retval.ToNative ("__result") + ";");

			bool fatal = parms.HasOutParam || !retval.IsVoid;
			sw.WriteLine ("\t\t\t} catch (Exception e) {");
			sw.WriteLine ("\t\t\t\tGLib.ExceptionManager.RaiseUnhandledException (e, " + (fatal ? "true" : "false") + ");");
			if (fatal) {
				sw.WriteLine ("\t\t\t\t// NOTREACHED: above call does not return.");
				sw.WriteLine ("\t\t\t\tthrow e;");
			}
			sw.WriteLine ("\t\t\t}");
			sw.WriteLine ("\t\t}");
			sw.WriteLine ();
		}

		public bool IsValid {
			get { 
				return Validate ();
			}
		}

		enum ValidState {
			Unvalidated,
			Invalid,
			Valid
		}

		ValidState vstate = ValidState.Unvalidated;

		public override bool Validate ()
		{
			if (vstate != ValidState.Unvalidated)
				return vstate == ValidState.Valid;

			vstate = ValidState.Valid;
			if (!parms.Validate () || !retval.Validate ()) {
				vstate = ValidState.Invalid;
			}

			if (vstate == ValidState.Invalid) {
				Console.WriteLine ("(in virtual method " + container_type.QualifiedName + "." + Name + ")");
				return false;
			} else {
				// The call string has to be created *after* the params have been validated since the Parameters class contains no elements before validation
				call = new ManagedCallString (parms);
				return true;
			}
		}
	}
}

