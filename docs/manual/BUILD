Thomas's new comments on the doc building :
-------------------------------------------
* originally the manual was written with DocBook 3.0 in mind, which 
  supported the graphic tag.  That is now deprecated, so I changed it to 
  the new mediaobject tag set.

* eps files in images/ should be generated from the makefile.  You need to 
  have fig2dev installed for that.


Wtay's original comments :
--------------------------

For now use:

  db2html gstreamer-manual 

(On debian, db2html is in the cygnus-stylesheets package)

You will need the png support for docbook (see GNOME documentation project)

convert the fig images to png with:

  fig2dev -L png -s 16 fig/<input file>.fig images/<input file>.png

Put a link in the gstreamer-manual directory with

  ln -s ../images gstreamer-manual/images

point your browser to gstreamer-manual/gstreamer.html

Fix typing errors and correct bad english.
Let me know about the stuff that needs some more explanation.
Let me know about the structure of the document.
