/* Gnome-Streamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifndef __GST_EDITOR_PROJECT_H__
#define __GST_EDITOR_PROJECT_H__

#include <gst/gst.h>
#include "gsteditor.h"
#include "gsteditorproperty.h"

#define GST_TYPE_EDITOR_PROJECT \
  (gst_editor_project_get_type())
#define GST_EDITOR_PROJECT(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_EDITOR_PROJECT,GstEditorProject))
#define GST_EDITOR_PROJECT_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_EDITOR_PROJECT,GstEditorProject))
#define GST_IS_EDITOR_PROJECT(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_EDITOR_PROJECT))
#define GST_IS_EDITOR_PROJECT_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_EDITOR_PROJECT))

typedef struct _GstEditorProject GstEditorProject;
typedef struct _GstEditorProjectClass GstEditorProjectClass;

struct _GstEditorProject {
  GtkObject object;

  GList *toplevelelements;
};

struct _GstEditorProjectClass {
  GtkObjectClass parent_class;

  void (*element_added)   (GstEditorProject *project,
		           GstEditorElement *element);
  void (*element_removed) (GstEditorProject *project,
		           GstEditorElement *element);
  void (*element_changed) (GstEditorProject *project,
		           GstEditorElement *element);
};

GtkType 		gst_editor_project_get_type		(void);

GstEditorProject*	gst_editor_project_new			(void);
GstEditorProject*	gst_editor_project_new_from_file	(const guchar *fname);
void			gst_editor_project_save			(GstEditorProject *project);
void			gst_editor_project_save_as		(GstEditorProject *project,
		   						 const guchar *fname);

void 			gst_editor_project_add_toplevel_element	(GstEditorProject *project, 
								 GstElement *element);

#define GST_TYPE_EDITOR_PROJECT_VIEW \
  (gst_editor_project_view_get_type())
#define GST_EDITOR_PROJECT_VIEW(obj) \
  (GTK_CHECK_CAST((obj),GST_TYPE_EDITOR_PROJECT_VIEW,GstEditorProjectView))
#define GST_EDITOR_PROJECT_VIEW_CLASS(klass) \
  (GTK_CHECK_CLASS_CAST((klass),GST_TYPE_EDITOR_PROJECT_VIEW,GstEditorProjectView))
#define GST_IS_EDITOR_PROJECT_VIEW(obj) \
  (GTK_CHECK_TYPE((obj),GST_TYPE_EDITOR_PROJECT_VIEW))
#define GST_IS_EDITOR_PROJECT_VIEW_CLASS(obj) \
  (GTK_CHECK_CLASS_TYPE((klass),GST_TYPE_EDITOR_PROJECT_VIEW))

typedef struct _GstEditorProjectView GstEditorProjectView;
typedef struct _GstEditorProjectViewClass GstEditorProjectViewClass;

struct _GstEditorProjectView {
  GtkObject object;

  GladeXML *xml;
  GtkWidget *list;
  GstEditorProject *project;
};

struct _GstEditorProjectViewClass {
  GtkObjectClass parent_class;
};

GtkType 		gst_editor_project_view_get_type	(void);

GstEditorProjectView*	gst_editor_project_view_new		(GstEditorProject *project);

#endif /* __GST_EDITOR_PROJECT_H__ */
