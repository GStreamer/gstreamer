/* GStreamer
 * Copyright (C) 2003 Erik Walthinsen <omega@cse.ogi.edu>
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

#include <gst/gst_private.h>
#include <gst/gstversion.h>
#include <gst/gstplugin.h>
#include <gst/gstindex.h>

#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>

#define GST_TYPE_FILE_INDEX		\
  (gst_file_index_get_type ())
#define GST_FILE_INDEX(obj)		\
  (G_TYPE_CHECK_INSTANCE_CAST ((obj), GST_TYPE_FILE_INDEX, GstFileIndex))
#define GST_FILE_INDEX_CLASS(klass)	\
  (G_TYPE_CHECK_CLASS_CAST ((klass), GST_TYPE_FILE_INDEX, GstFileIndexClass))
#define GST_IS_FILE_INDEX(obj)		\
  (G_TYPE_CHECK_INSTANCE_TYPE ((obj), GST_TYPE_FILE_INDEX))
#define GST_IS_FILE_INDEX_CLASS(obj)	\
  (GST_TYPE_CHECK_CLASS_TYPE ((klass), GST_TYPE_FILE_INDEX))
	
/*
 * Object model:
 *
 * We build an index to each entry for each id.
 * 
 *
 *  fileindex
 *    -----------------------------...
 *    !                  !         
 *   id1                 id2        
 *    !
 *   GArray
 *
 * The fileindex creates a FileIndexId object for each writer id, a
 * Hashtable is kept to map the id to the FileIndexId
 *
 * The FileIndexId also keeps all the values in a sorted GArray.
 *
 * Finding a value for an id/format requires locating the correct GArray,
 * then do a binary search to get the required value.
 *
 * Unlike gstmemindex:  All formats are assumed to sort to the
 * same order.  All formats are assumed to be available from
 * any entry.
 */

/*
 * Each array element is (32bits flags, nformats * 64bits)
 */
typedef struct {
  gint 		 id;
  gchar         *id_desc;
  gint           nformats;
  GstFormat 	*format;
  GArray	*array;
} GstFileIndexId;

typedef struct _GstFileIndex GstFileIndex;
typedef struct _GstFileIndexClass GstFileIndexClass;

#define ARRAY_ROW_SIZE(_ii) \
  (sizeof (gint32) + (_ii)->nformats * sizeof (gint64))
#define ARRAY_TOTAL_SIZE(_ii) \
  (_ii->array->len * ARRAY_ROW_SIZE(_ii))

// don't forget to convert to/from BE byte-order
#define ARRAY_ROW_FLAGS(_row) \
  (*((gint32*) (_row)))
#define ARRAY_ROW_VALUE(_row,_vx) \
  (*(gint64*) (((gchar*)(_row)) + sizeof (gint32) + (_vx) * sizeof (gint64)))

struct _GstFileIndex {
  GstIndex		 parent;

  gchar                 *location;
  gboolean               is_loaded;
  GSList                *unresolved;
  GHashTable		*id_index;

  GstIndexEntry         *ret_entry;  // hack to avoid leaking memory
};

struct _GstFileIndexClass {
  GstIndexClass parent_class;
};

enum {
  ARG_0,
  ARG_LOCATION,
};

static void		gst_file_index_class_init	(GstFileIndexClass *klass);
static void		gst_file_index_init		(GstFileIndex *index);
static void 		gst_file_index_dispose 		(GObject *object);

static void
gst_file_index_set_property (GObject *object,
			     guint prop_id,
			     const GValue *value,
			     GParamSpec *pspec);
static void
gst_file_index_get_property (GObject *object,
			     guint prop_id,
			     GValue *value,
			     GParamSpec *pspec);

static gboolean
gst_file_index_get_writer_id  (GstIndex *_index, gint *id, gchar *writer_string);

static void             gst_file_index_commit           (GstIndex *index, gint writer_id);
static void 		gst_file_index_add_entry 	(GstIndex *index, GstIndexEntry *entry);
static GstIndexEntry* 	gst_file_index_get_assoc_entry 	(GstIndex *index, gint id,
                              				 GstIndexLookupMethod method,
							 GstAssocFlags flags,
                              				 GstFormat format, gint64 value,
                              				 GCompareDataFunc func,
                              				 gpointer user_data);

#define CLASS(file_index)  GST_FILE_INDEX_CLASS (G_OBJECT_GET_CLASS (file_index))

static GstIndex *parent_class = NULL;

GType
gst_file_index_get_type(void) {
  static GType file_index_type = 0;

  if (!file_index_type) {
    static const GTypeInfo file_index_info = {
      sizeof(GstFileIndexClass),
      NULL,
      NULL,
      (GClassInitFunc)gst_file_index_class_init,
      NULL,
      NULL,
      sizeof(GstFileIndex),
      1,
      (GInstanceInitFunc)gst_file_index_init,
      NULL
    };
    file_index_type = g_type_register_static(GST_TYPE_INDEX, "GstFileIndex", &file_index_info, 0);
  }
  return file_index_type;
}

static void
gst_file_index_class_init (GstFileIndexClass *klass)
{
  GObjectClass *gobject_class;
  GstIndexClass *gstindex_class;

  gobject_class = (GObjectClass*)klass;
  gstindex_class = (GstIndexClass*)klass;

  parent_class = g_type_class_ref(GST_TYPE_INDEX);

  gobject_class->dispose        = gst_file_index_dispose;
  gobject_class->set_property 	= gst_file_index_set_property;
  gobject_class->get_property 	= gst_file_index_get_property;

  gstindex_class->add_entry 	  = gst_file_index_add_entry;
  gstindex_class->get_assoc_entry = gst_file_index_get_assoc_entry;
  gstindex_class->commit 	  = gst_file_index_commit;
  gstindex_class->get_writer_id   = gst_file_index_get_writer_id ;

  g_object_class_install_property (gobject_class, ARG_LOCATION,
   g_param_spec_string ("location", "File Location",
			"Location of the index file",
			NULL, G_PARAM_READWRITE));
}

static void
gst_file_index_init (GstFileIndex *index)
{
  GST_DEBUG(0, "created new file index");

  index->id_index = g_hash_table_new (g_int_hash, g_int_equal);
}

static void
gst_file_index_dispose (GObject *object)
{
  GstFileIndex *index = GST_FILE_INDEX (object);

  if (index->location)
    g_free (index->location);

  // need to take care when destroying garrays with mmap'd segments

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static gboolean
gst_file_index_get_writer_id  (GstIndex *_index, 
			       gint *id, gchar *writer_string)
{
  GstFileIndex *index = GST_FILE_INDEX (_index);
  GSList *pending = index->unresolved;
  gboolean match = FALSE;
  GSList *elem;

  if (!index->is_loaded)
    return TRUE;

  g_return_val_if_fail (id, FALSE);
  g_return_val_if_fail (writer_string, FALSE);

  index->unresolved = NULL;

  for (elem = pending; elem; elem = g_slist_next (elem)) {
    GstFileIndexId *ii = elem->data;
    if (strcmp (ii->id_desc, writer_string) != 0) {
      index->unresolved = g_slist_prepend (index->unresolved, ii);
      continue;
    }
    
    if (match) {
      g_warning ("Duplicate matches for writer '%s'", writer_string);
      continue;
    }

    //g_warning ("resolve %d %s", *id, writer_string);
    ii->id = *id;
    g_hash_table_insert (index->id_index, id, ii);
    match = TRUE;
  }

  g_slist_free (pending);

  return match;
}

static void
_fc_alloc_array (GstFileIndexId *id_index)
{
  g_assert (!id_index->array);
  id_index->array =
    g_array_sized_new (FALSE, FALSE, ARRAY_ROW_SIZE (id_index), 0);
}

static void
gst_file_index_load (GstFileIndex *index)
{
  xmlDocPtr doc;
  xmlNodePtr root, part;
  xmlChar *val;

  g_assert (index->location);
  g_return_if_fail (!index->is_loaded);

  {
    gchar *path = g_strdup_printf ("%s/gstindex.xml", index->location);
    GError *err = NULL;
    gchar *buf;
    gsize len;
    g_file_get_contents (path, &buf, &len, &err);
    g_free (path);
    if (err) g_error ("%s", err->message);

    doc = xmlParseMemory (buf, len);
    g_free (buf);
  }

  //xmlDocFormatDump (stderr, doc, TRUE);

  root = doc->xmlRootNode;
  if (strcmp (root->name, "gstfileindex") != 0)
    g_error ("root node isn't a gstfileindex");
  
  val = xmlGetProp (root, "version");
  if (!val || atoi (val) != 1)
    g_error ("version != 1");
  free (val);

  for (part = root->children; part; part = part->next) {
    if (strcmp (part->name, "writers") == 0) {
      xmlNodePtr writer;
      for (writer = part->children; writer; writer = writer->next) {
	xmlChar *datafile = xmlGetProp (writer, "datafile");
	gchar *path = g_strdup_printf ("%s/%s", index->location, datafile);
	int fd;
	GstFileIndexId *id_index;
	xmlNodePtr wpart;
	xmlChar *entries_str;
	gpointer array_data;

	free (datafile);

	fd = open (path, O_RDONLY);
	g_free (path);
	if (fd < 0) {
	  g_warning ("Can't open '%s': %s", path, strerror (errno));
	  continue;
	}

	id_index = g_new0 (GstFileIndexId, 1);
	id_index->id_desc = xmlGetProp (writer, "id");

	for (wpart = writer->children; wpart; wpart = wpart->next) {
	  if (strcmp (wpart->name, "formats") == 0) {
	    xmlChar *count_str = xmlGetProp (wpart, "count");
	    gint fx=0;
	    xmlNodePtr format;

	    id_index->nformats = atoi (count_str);
	    free (count_str);

	    id_index->format = g_new (GstFormat, id_index->nformats);

	    for (format = wpart->children;
		 format; format = format->next) {
	      xmlChar *nick = xmlGetProp (format, "nick");
	      GstFormat fmt = gst_format_get_by_nick (nick);
	      if (fmt == GST_FORMAT_UNDEFINED)
		g_error ("format '%s' undefined", nick);
	      g_assert (fx < id_index->nformats);
	      id_index->format[fx++] = fmt;
	      free (nick);
	    }
	  } else
	    g_warning ("unknown wpart '%s'", wpart->name);
	}

	g_assert (id_index->nformats > 0);
	_fc_alloc_array (id_index);
	g_assert (id_index->array->data == NULL);  // little bit risky

	entries_str = xmlGetProp (writer, "entries");
	id_index->array->len = atoi (entries_str);
	free (entries_str);

	array_data =
	  mmap (NULL, ARRAY_TOTAL_SIZE (id_index), PROT_READ, MAP_SHARED, fd, 0);
	close (fd);
	if (array_data == MAP_FAILED) {
	  g_error ("mmap %s failed: %s", path, strerror (errno));
	  continue;
	}

	id_index->array->data = array_data;

	index->unresolved = g_slist_prepend (index->unresolved, id_index);
      }
    } else
      g_warning ("unknown part '%s'", part->name);
  }

  xmlFreeDoc (doc);

  GST_FLAG_UNSET (index, GST_INDEX_WRITABLE);
  index->is_loaded = TRUE;
}

static void
gst_file_index_set_property (GObject *object,
			     guint prop_id,
			     const GValue *value,
			     GParamSpec *pspec)
{
  GstFileIndex *index = GST_FILE_INDEX (object);

  switch (prop_id) {
  case ARG_LOCATION:
    if (index->location)
      g_free (index->location);
    index->location = g_value_dup_string (value);

    if (index->location && !g_hash_table_size (index->id_index))
      gst_file_index_load (index);
    break;
  }
}

static void
gst_file_index_get_property (GObject *object,
			     guint prop_id,
			     GValue *value,
			     GParamSpec *pspec)
{
  GstFileIndex *index = GST_FILE_INDEX (object);
  
  switch (prop_id) {
  case ARG_LOCATION:
    g_value_set_string (value, index->location);
    break;
  }
}

static void
_file_index_id_save_xml (gpointer _key, GstFileIndexId *ii, xmlNodePtr writers)
{
  const gint bufsize = 16;
  gchar buf[bufsize];
  xmlNodePtr writer;
  xmlNodePtr formats;
  gint xx;
  
  writer = xmlNewChild (writers, NULL, "writer", NULL);
  xmlSetProp (writer, "id", ii->id_desc);
  g_snprintf (buf, bufsize, "%d", ii->array->len);
  xmlSetProp (writer, "entries", buf);
  g_snprintf (buf, bufsize, "%d", ii->id); // any unique number is OK
  xmlSetProp (writer, "datafile", buf);

  formats = xmlNewChild (writer, NULL, "formats", NULL);
  g_snprintf (buf, bufsize, "%d", ii->nformats);
  xmlSetProp (formats, "count", buf);

  for (xx=0; xx < ii->nformats; xx++) {
    xmlNodePtr format = xmlNewChild (formats, NULL, "format", NULL);
    const GstFormatDefinition* def =
      gst_format_get_details (ii->format[xx]);
    xmlSetProp (format, "nick", def->nick);
  }
}

//
// We must save the binary data in separate files because
// mmap wants getpagesize() alignment.  If we append all
// the data to one file then we don't know the appropriate
// padding since the page size isn't fixed.
//
static void
_file_index_id_save_entries (gpointer *_key,
			     GstFileIndexId *ii,
			     gchar *prefix)
{
  GError *err = NULL;
  gchar *path = g_strdup_printf ("%s/%d", prefix, ii->id);
  GIOChannel *chan =
    g_io_channel_new_file (path, "w", &err);
  g_free (path);
  if (err) g_error ("%s", err->message);
  
  g_io_channel_set_encoding (chan, NULL, &err);
  if (err) g_error ("%s", err->message);

  g_io_channel_write_chars (chan,
			    ii->array->data,
			    ARRAY_TOTAL_SIZE (ii),
			    NULL,
			    &err);
  if (err) g_error ("%s", err->message);

  g_io_channel_shutdown (chan, TRUE, &err);
  if (err) g_error ("%s", err->message);

  g_io_channel_unref (chan);
}

// We have to save the whole set of indexes into a single file
// so it doesn't make sense to commit only a single writer.
//
// i suggest:
//
// gst_index_commit (index, -1);

static void
gst_file_index_commit (GstIndex *_index, gint _writer_id)
{
  GstFileIndex *index = GST_FILE_INDEX (_index);
  xmlDocPtr doc;
  xmlNodePtr writers;
  GError *err = NULL;
  gchar *path;
  GIOChannel *tocfile;

  g_return_if_fail (index->location);
  g_return_if_fail (!index->is_loaded);

  GST_FLAG_UNSET (index, GST_INDEX_WRITABLE);

  doc = xmlNewDoc ("1.0");
  doc->xmlRootNode = xmlNewDocNode (doc, NULL, "gstfileindex", NULL);
  xmlSetProp (doc->xmlRootNode, "version", "1");

  writers = xmlNewChild (doc->xmlRootNode, NULL, "writers", NULL);
  g_hash_table_foreach (index->id_index,
			(GHFunc) _file_index_id_save_xml, writers);

  if (mkdir (index->location, 0777) &&
      errno != EEXIST)
    g_error ("mkdir %s: %s", index->location, strerror (errno));

  path = g_strdup_printf ("%s/gstindex.xml", index->location);
  tocfile =
    g_io_channel_new_file (path, "w", &err);
  g_free (path);
  if (err) g_error ("%s", err->message);

  g_io_channel_set_encoding (tocfile, NULL, &err);
  if (err) g_error ("%s", err->message);

  {
    xmlChar *xmlmem;
    int xmlsize;
    xmlDocDumpMemory (doc, &xmlmem, &xmlsize);
    g_io_channel_write_chars (tocfile, xmlmem, xmlsize, NULL, &err);
    if (err) g_error ("%s", err->message);
    xmlFreeDoc (doc);
    free (xmlmem);
  }

  g_io_channel_shutdown (tocfile, TRUE, &err);
  if (err) g_error ("%s", err->message);

  g_io_channel_unref (tocfile);

  g_hash_table_foreach (index->id_index,
			(GHFunc) _file_index_id_save_entries,
			index->location);
}

static void
gst_file_index_add_id (GstIndex *index, GstIndexEntry *entry)
{
  GstFileIndex *fileindex = GST_FILE_INDEX (index);
  GstFileIndexId *id_index;

  id_index = g_hash_table_lookup (fileindex->id_index, &entry->id);

  if (!id_index) {
    id_index = g_new0 (GstFileIndexId, 1);

    id_index->id = entry->id;
    id_index->id_desc = g_strdup (entry->data.id.description);

    // It would be useful to know the GType of the writer so
    // we can try to cope with changes in the id_desc path.

    g_hash_table_insert (fileindex->id_index, &entry->id, id_index);
  }
}

// This algorithm differs from libc bsearch in the handling
// of non-exact matches.

static gboolean
_fc_bsearch (GArray *          ary,
	     gint *            ret,
	     GCompareDataFunc  compare,
	     gconstpointer     sample,
	     gpointer          user_data)
{
  gint first, last;
  gint mid;
  gint midsize;
  gint cmp;
  gint tx;

  g_return_val_if_fail (compare, FALSE);

  if (!ary->len)
    {
      if (ret) *ret = 0;
      return FALSE;
    }

  first = 0;
  last = ary->len - 1;

  midsize = last - first;
  
  while (midsize > 1) {
    mid = first + midsize / 2;
    
    cmp = (*compare) (sample, &g_array_index (ary, char, mid), user_data);
    
    if (cmp == 0)
      {
	// if there are multiple matches then scan for the first match
	while (mid > 0 &&
	       (*compare) (sample,
			   &g_array_index (ary, char, mid - 1),
			   user_data) == 0)
	  --mid;

	if (ret) *ret = mid;
	return TRUE;
      }
    
    if (cmp < 0)
      last = mid-1;
    else
      first = mid+1;
    
    midsize = last - first;
  }

  for (tx = first; tx <= last; tx++)
    {
      cmp = (*compare) (sample, &g_array_index (ary, char, tx), user_data);

      if (cmp < 0)
	{
	  if (ret) *ret = tx;
	  return FALSE;
	}
      if (cmp == 0)
	{
	  if (ret) *ret = tx;
	  return TRUE;
	}
    }

  if (ret) *ret = last+1;
  return FALSE;
}

static gint
file_index_compare (gconstpointer sample,
		    gconstpointer row,
		    gpointer user_data)
{
  //GstFileIndexId *id_index = user_data;
  const GstIndexAssociation *ca = sample;
  gint64 val1 = ca->value;
  gint64 val2 = GINT64_FROM_BE (ARRAY_ROW_VALUE (row, ca->format));
  gint64 diff = val2 - val1;
  return (diff == 0 ? 0 : (diff > 0 ? 1 : -1));
}

static void
gst_file_index_add_association (GstIndex *index, GstIndexEntry *entry)
{
  GstFileIndex *fileindex = GST_FILE_INDEX (index);
  GstFileIndexId *id_index;
  gint mx;
  GstIndexAssociation sample;
  gboolean exact;
  gint fx;

  id_index = g_hash_table_lookup (fileindex->id_index, &entry->id);
  if (!id_index)
    return;

  if (!id_index->nformats) {
    gint fx;
    id_index->nformats = GST_INDEX_NASSOCS (entry);
    //g_warning ("%d: formats = %d", entry->id, id_index->nformats);
    id_index->format = g_new (GstFormat, id_index->nformats);
    for (fx=0; fx < id_index->nformats; fx++)
      id_index->format[fx] = GST_INDEX_ASSOC_FORMAT (entry, fx);
    _fc_alloc_array (id_index);
  } else {
    /* only sanity checking */
    if (id_index->nformats != GST_INDEX_NASSOCS (entry))
      g_warning ("fileindex arity change %d -> %d",
		 id_index->nformats, GST_INDEX_NASSOCS (entry));
    else {
      gint fx;
      for (fx=0; fx < id_index->nformats; fx++)
	if (id_index->format[fx] != GST_INDEX_ASSOC_FORMAT (entry, fx))
	  g_warning ("fileindex format[%d] changed %d -> %d",
		     fx,
		     id_index->format[fx],
		     GST_INDEX_ASSOC_FORMAT (entry, fx));
    }
  }

  /* this is a hack, we should use a private structure instead */
  sample.format = 0;
  sample.value = GST_INDEX_ASSOC_VALUE (entry, 0);

  exact =
    _fc_bsearch (id_index->array, &mx, file_index_compare,
		 &sample, id_index);

  if (exact) {
    // maybe overwrite instead?
    g_warning ("ignoring duplicate index association at %lld",
	       GST_INDEX_ASSOC_VALUE (entry, 0));
    return;
  }

  // should verify that all formats are ordered XXX

  {
    gchar row_data[ARRAY_ROW_SIZE (id_index)];

    ARRAY_ROW_FLAGS (row_data) =
      GINT32_TO_BE (GST_INDEX_ASSOC_FLAGS (entry));

    for (fx = 0; fx < id_index->nformats; fx++)
      ARRAY_ROW_VALUE (row_data, fx) =
        GINT64_TO_BE (GST_INDEX_ASSOC_VALUE (entry, fx));

    g_array_insert_val (id_index->array, mx, row_data);
  }
}

/*
static void
show_entry (GstIndexEntry *entry)
{
  switch (entry->type) {
    case GST_INDEX_ENTRY_ID:
      g_print ("id %d describes writer %s\n", entry->id, 
		      GST_INDEX_ID_DESCRIPTION (entry));
      break;
    case GST_INDEX_ENTRY_FORMAT:
      g_print ("%d: registered format %d for %s\n", entry->id, 
		      GST_INDEX_FORMAT_FORMAT (entry),
		      GST_INDEX_FORMAT_KEY (entry));
      break;
    case GST_INDEX_ENTRY_ASSOCIATION:
    {
      gint i;

      g_print ("%d: %08x ", entry->id, GST_INDEX_ASSOC_FLAGS (entry));
      for (i = 0; i < GST_INDEX_NASSOCS (entry); i++) {
	g_print ("%d %lld ", GST_INDEX_ASSOC_FORMAT (entry, i), 
			     GST_INDEX_ASSOC_VALUE (entry, i));
      }
      g_print ("\n");
      break;
    }
    default:
      break;
  }
}
*/

static void
gst_file_index_add_entry (GstIndex *index, GstIndexEntry *entry)
{
  GstFileIndex *fileindex = GST_FILE_INDEX (index);

  GST_DEBUG (0, "adding entry %p\n", fileindex);

  //show_entry (entry);

  switch (entry->type){
     case GST_INDEX_ENTRY_ID:
       gst_file_index_add_id (index, entry);
       break;
     case GST_INDEX_ENTRY_ASSOCIATION:
       gst_file_index_add_association (index, entry);
       break;
     case GST_INDEX_ENTRY_OBJECT:
       g_error ("gst_file_index_add_object not implemented");
       break;
     case GST_INDEX_ENTRY_FORMAT:
       g_warning ("gst_file_index_add_format not implemented");
       break;
     default:
       break;
  }
}

static GstIndexEntry*
gst_file_index_get_assoc_entry (GstIndex *index,
				gint id,
				GstIndexLookupMethod method,
				GstAssocFlags flags,
				GstFormat format,
				gint64 value,
				GCompareDataFunc _ignore_func,
				gpointer _ignore_user_data)
{
  GstFileIndex *fileindex = GST_FILE_INDEX (index);
  GstFileIndexId *id_index;
  gint formatx = -1;
  gint fx;
  GstIndexAssociation sample;
  gint mx;
  gboolean exact;
  gpointer row_data;
  GstIndexEntry *entry;
  gint xx;

  id_index = g_hash_table_lookup (fileindex->id_index, &id);
  if (!id_index)
    return NULL;

  for (fx=0; fx < id_index->nformats; fx++)
    if (id_index->format[fx] == format)
      { formatx = fx; break; }

  if (formatx == -1) {
    g_warning ("index does not contain format %d", format);
    return NULL;
  }

  /* this is a hack, we should use a private structure instead */
  sample.format = formatx;
  sample.value = value;

  exact =  _fc_bsearch (id_index->array, &mx, file_index_compare,
		 &sample, id_index);

  if (!exact) {
    if (method == GST_INDEX_LOOKUP_EXACT)
      return NULL;
    else if (method == GST_INDEX_LOOKUP_BEFORE) {
      if (mx == 0)
	return NULL;
      mx -= 1;
    } else if (method == GST_INDEX_LOOKUP_AFTER) {
      if (mx == id_index->array->len)
	return NULL;
    }
  }

  row_data = &g_array_index (id_index->array, char, mx);

  // if exact then ignore flags (?)
  if (method != GST_INDEX_LOOKUP_EXACT)
    while ((GINT32_FROM_BE (ARRAY_ROW_FLAGS (row_data)) & flags) != flags) {
      if (method == GST_INDEX_LOOKUP_BEFORE)
	mx -= 1;
      else if (method == GST_INDEX_LOOKUP_AFTER)
	mx += 1;
      if (mx < 0 || mx >= id_index->array->len)
	return NULL;
      row_data = &g_array_index (id_index->array, char, mx);
    }

  // entry memory management needs improvement
  if (!fileindex->ret_entry)
    fileindex->ret_entry = g_new0 (GstIndexEntry, 1);
  entry = fileindex->ret_entry;
  if (entry->data.assoc.assocs)
    g_free (entry->data.assoc.assocs);

  entry->type = GST_INDEX_ENTRY_ASSOCIATION;

  GST_INDEX_NASSOCS (entry) = id_index->nformats;
  entry->data.assoc.assocs =
    g_new (GstIndexAssociation, id_index->nformats);

  GST_INDEX_ASSOC_FLAGS (entry) =
    GINT32_FROM_BE (ARRAY_ROW_FLAGS (row_data));

  for (xx=0; xx < id_index->nformats; xx++) 
    {
      GST_INDEX_ASSOC_FORMAT (entry, xx) = id_index->format[xx];
      GST_INDEX_ASSOC_VALUE (entry, xx) =
	GINT64_FROM_BE (ARRAY_ROW_VALUE (row_data, xx));
    }

  return entry;
}

gboolean
gst_file_index_plugin_init (GModule *module, GstPlugin *plugin)
{
  GstIndexFactory *factory;

  gst_plugin_set_longname (plugin, "A file index");

  factory = gst_index_factory_new ("fileindex",
	                           "A index that stores entries in file",
                                   gst_file_index_get_type());

  if (factory != NULL) {
    gst_plugin_add_feature (plugin, GST_PLUGIN_FEATURE (factory));
  }
  else {
    g_warning ("could not register fileindex");
  }
  return TRUE;
}
