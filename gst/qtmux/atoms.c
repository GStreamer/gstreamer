/* Quicktime muxer plugin for GStreamer
 * Copyright (C) 2008 Thiago Sousa Santos <thiagoss@embedded.ufcg.edu.br>
 * Copyright (C) 2008 Mark Nauwelaerts <mnauw@users.sf.net>
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
/*
 * Unless otherwise indicated, Source Code is licensed under MIT license.
 * See further explanation attached in License Statement (distributed in the file
 * LICENSE).
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "atoms.h"
#include <string.h>
#include <glib.h>

#include <gst/gst.h>
#include <gst/base/gstbytewriter.h>

/**
 * Creates a new AtomsContext for the given flavor.
 */
AtomsContext *
atoms_context_new (AtomsTreeFlavor flavor)
{
  AtomsContext *context = g_new0 (AtomsContext, 1);
  context->flavor = flavor;
  return context;
}

/**
 * Frees an AtomsContext and all memory associated with it
 */
void
atoms_context_free (AtomsContext * context)
{
  g_free (context);
}

/* -- creation, initialization, clear and free functions -- */

#define SECS_PER_DAY (24 * 60 * 60)
#define LEAP_YEARS_FROM_1904_TO_1970 17

static guint64
get_current_qt_time ()
{
  GTimeVal timeval;

  g_get_current_time (&timeval);
  /* FIXME this should use UTC coordinated time */
  return timeval.tv_sec + (((1970 - 1904) * (guint64) 365) +
      LEAP_YEARS_FROM_1904_TO_1970) * SECS_PER_DAY;
}

static void
common_time_info_init (TimeInfo * ti)
{
  ti->creation_time = ti->modification_time = get_current_qt_time ();
  ti->timescale = 0;
  ti->duration = 0;
}

static void
atom_header_set (Atom * header, guint32 fourcc, gint32 size, gint64 ext_size)
{
  header->type = fourcc;
  header->size = size;
  header->extended_size = ext_size;
}

static void
atom_clear (Atom * atom)
{
}

static void
atom_full_init (AtomFull * full, guint32 fourcc, gint32 size, gint64 ext_size,
    guint8 version, guint8 flags[3])
{
  atom_header_set (&(full->header), fourcc, size, ext_size);
  full->version = version;
  full->flags[0] = flags[0];
  full->flags[1] = flags[1];
  full->flags[2] = flags[2];
}

static void
atom_full_clear (AtomFull * full)
{
  atom_clear (&full->header);
}

static void
atom_full_free (AtomFull * full)
{
  atom_full_clear (full);
  g_free (full);
}

static AtomInfo *
build_atom_info_wrapper (Atom * atom, gpointer copy_func, gpointer free_func)
{
  AtomInfo *info = NULL;

  if (atom) {
    info = g_new0 (AtomInfo, 1);

    info->atom = atom;
    info->copy_data_func = copy_func;
    info->free_func = free_func;
  }

  return info;
}

static GList *
atom_info_list_prepend_atom (GList * ai, Atom * atom,
    AtomCopyDataFunc copy_func, AtomFreeFunc free_func)
{
  if (atom)
    return g_list_prepend (ai,
        build_atom_info_wrapper (atom, copy_func, free_func));
  else
    return ai;
}

static void
atom_info_list_free (GList * ai)
{
  while (ai) {
    AtomInfo *info = (AtomInfo *) ai->data;

    info->free_func (info->atom);
    g_free (info);
    ai = g_list_delete_link (ai, ai);
  }
}

static AtomData *
atom_data_new (guint32 fourcc)
{
  AtomData *data = g_new0 (AtomData, 1);

  atom_header_set (&data->header, fourcc, 0, 0);
  return data;
}

static void
atom_data_alloc_mem (AtomData * data, guint32 size)
{
  if (data->data) {
    g_free (data->data);
  }
  data->data = g_new0 (guint8, size);
  data->datalen = size;
}

static AtomData *
atom_data_new_from_gst_buffer (guint32 fourcc, const GstBuffer * buf)
{
  AtomData *data = atom_data_new (fourcc);

  atom_data_alloc_mem (data, GST_BUFFER_SIZE (buf));
  g_memmove (data->data, GST_BUFFER_DATA (buf), GST_BUFFER_SIZE (buf));
  return data;
}

static void
atom_data_free (AtomData * data)
{
  atom_clear (&data->header);
  g_free (data->data);
  g_free (data);
}

static void
atom_ftyp_init (AtomFTYP * ftyp, guint32 major, guint32 version, GList * brands)
{
  gint index;
  GList *it = NULL;

  atom_header_set (&ftyp->header, FOURCC_ftyp, 16, 0);
  ftyp->major_brand = major;
  ftyp->version = version;

  /* always include major brand as compatible brand */
  ftyp->compatible_brands_size = g_list_length (brands) + 1;
  ftyp->compatible_brands = g_new (guint32, ftyp->compatible_brands_size);

  ftyp->compatible_brands[0] = major;
  index = 1;
  for (it = brands; it != NULL; it = g_list_next (it)) {
    ftyp->compatible_brands[index++] = GPOINTER_TO_UINT (it->data);
  }
}

AtomFTYP *
atom_ftyp_new (AtomsContext * context, guint32 major, guint32 version,
    GList * brands)
{
  AtomFTYP *ftyp = g_new0 (AtomFTYP, 1);

  atom_ftyp_init (ftyp, major, version, brands);
  return ftyp;
}

void
atom_ftyp_free (AtomFTYP * ftyp)
{
  atom_clear (&ftyp->header);
  g_free (ftyp->compatible_brands);
  ftyp->compatible_brands = NULL;
  g_free (ftyp);
}

static void
atom_esds_init (AtomESDS * esds)
{
  guint8 flags[3] = { 0, 0, 0 };

  atom_full_init (&esds->header, FOURCC_esds, 0, 0, 0, flags);
  desc_es_init (&esds->es);
}

static AtomESDS *
atom_esds_new ()
{
  AtomESDS *esds = g_new0 (AtomESDS, 1);

  atom_esds_init (esds);
  return esds;
}

static void
atom_esds_free (AtomESDS * esds)
{
  atom_full_clear (&esds->header);
  desc_es_descriptor_clear (&esds->es);
  g_free (esds);
}

static AtomFRMA *
atom_frma_new ()
{
  AtomFRMA *frma = g_new0 (AtomFRMA, 1);

  atom_header_set (&frma->header, FOURCC_frma, 0, 0);
  return frma;
}

static void
atom_frma_free (AtomFRMA * frma)
{
  atom_clear (&frma->header);
  g_free (frma);
}

static AtomWAVE *
atom_wave_new ()
{
  AtomWAVE *wave = g_new0 (AtomWAVE, 1);

  atom_header_set (&wave->header, FOURCC_wave, 0, 0);
  return wave;
}

static void
atom_wave_free (AtomWAVE * wave)
{
  atom_clear (&wave->header);
  atom_info_list_free (wave->extension_atoms);
  g_free (wave);
}

static void
atom_elst_init (AtomELST * elst)
{
  guint8 flags[3] = { 0, 0, 0 };
  atom_full_init (&elst->header, FOURCC_elst, 0, 0, 0, flags);
  elst->entries = 0;
}

static void
atom_elst_clear (AtomELST * elst)
{
  GSList *walker;

  atom_full_clear (&elst->header);
  walker = elst->entries;
  while (walker) {
    g_free ((EditListEntry *) walker->data);
    walker = g_slist_next (walker);
  }
  g_slist_free (elst->entries);
}

static void
atom_edts_init (AtomEDTS * edts)
{
  atom_header_set (&edts->header, FOURCC_edts, 0, 0);
  atom_elst_init (&edts->elst);
}

static void
atom_edts_clear (AtomEDTS * edts)
{
  atom_clear (&edts->header);
  atom_elst_clear (&edts->elst);
}

AtomEDTS *
atom_edts_new ()
{
  AtomEDTS *edts = g_new0 (AtomEDTS, 1);
  atom_edts_init (edts);
  return edts;
}

void
atom_edts_free (AtomEDTS * edts)
{
  atom_edts_clear (edts);
  g_free (edts);
}

static void
atom_sample_entry_init (SampleTableEntry * se, guint32 type)
{
  atom_header_set (&se->header, type, 0, 0);

  memset (se->reserved, 0, sizeof (guint8) * 6);
  se->data_reference_index = 0;
}

static void
atom_sample_entry_free (SampleTableEntry * se)
{
  atom_clear (&se->header);
}

static void
sample_entry_mp4a_init (SampleTableEntryMP4A * mp4a)
{
  atom_sample_entry_init (&mp4a->se, FOURCC_mp4a);

  mp4a->version = 0;
  mp4a->revision_level = 0;
  mp4a->vendor = 0;
  mp4a->channels = 2;
  mp4a->sample_size = 16;
  mp4a->compression_id = 0;
  mp4a->packet_size = 0;
  mp4a->sample_rate = 0;
  /* following only used if version is 1 */
  mp4a->samples_per_packet = 0;
  mp4a->bytes_per_packet = 0;
  mp4a->bytes_per_frame = 0;
  mp4a->bytes_per_sample = 0;

  mp4a->extension_atoms = NULL;
}

static SampleTableEntryMP4A *
sample_entry_mp4a_new ()
{
  SampleTableEntryMP4A *mp4a = g_new0 (SampleTableEntryMP4A, 1);

  sample_entry_mp4a_init (mp4a);
  return mp4a;
}

static void
sample_entry_mp4a_free (SampleTableEntryMP4A * mp4a)
{
  atom_sample_entry_free (&mp4a->se);
  atom_info_list_free (mp4a->extension_atoms);
  g_free (mp4a);
}

static void
sample_entry_mp4v_init (SampleTableEntryMP4V * mp4v, AtomsContext * context)
{
  atom_sample_entry_init (&mp4v->se, FOURCC_mp4v);

  mp4v->version = 0;
  mp4v->revision_level = 0;
  mp4v->vendor = 0;

  mp4v->temporal_quality = 0;
  mp4v->spatial_quality = 0;

  /* qt and ISO base media do not contradict, and examples agree */
  mp4v->horizontal_resolution = 0x00480000;
  mp4v->vertical_resolution = 0x00480000;

  mp4v->datasize = 0;
  mp4v->frame_count = 1;

  memset (mp4v->compressor, 0, sizeof (guint8) * 32);

  mp4v->depth = 0;
  mp4v->color_table_id = 0;

  mp4v->extension_atoms = NULL;
}

static void
sample_entry_mp4v_free (SampleTableEntryMP4V * mp4v)
{
  atom_sample_entry_free (&mp4v->se);
  atom_info_list_free (mp4v->extension_atoms);
  g_free (mp4v);
}

static SampleTableEntryMP4V *
sample_entry_mp4v_new (AtomsContext * context)
{
  SampleTableEntryMP4V *mp4v = g_new0 (SampleTableEntryMP4V, 1);

  sample_entry_mp4v_init (mp4v, context);
  return mp4v;
}

static void
atom_stsd_init (AtomSTSD * stsd)
{
  guint8 flags[3] = { 0, 0, 0 };

  atom_full_init (&stsd->header, FOURCC_stsd, 0, 0, 0, flags);
  stsd->entries = NULL;
}

static void
atom_stsd_clear (AtomSTSD * stsd)
{
  GList *walker;

  atom_full_clear (&stsd->header);
  walker = stsd->entries;
  while (walker) {
    GList *aux = walker;
    SampleTableEntry *se = (SampleTableEntry *) aux->data;

    walker = g_list_next (walker);
    stsd->entries = g_list_remove_link (stsd->entries, aux);

    switch (se->kind) {
      case AUDIO:
        sample_entry_mp4a_free ((SampleTableEntryMP4A *) se);
        break;
      case VIDEO:
        sample_entry_mp4v_free ((SampleTableEntryMP4V *) se);
        break;
      default:
        /* best possible cleanup */
        atom_sample_entry_free (se);
    }
    g_list_free (aux);
  }
}

static void
atom_ctts_init (AtomCTTS * ctts)
{
  guint8 flags[3] = { 0, 0, 0 };

  atom_full_init (&ctts->header, FOURCC_ctts, 0, 0, 0, flags);
  ctts->entries = NULL;
}

static AtomCTTS *
atom_ctts_new ()
{
  AtomCTTS *ctts = g_new0 (AtomCTTS, 1);

  atom_ctts_init (ctts);
  return ctts;
}

static void
atom_ctts_free (AtomCTTS * ctts)
{
  GList *walker;

  atom_full_clear (&ctts->header);
  walker = ctts->entries;
  while (walker) {
    GList *aux = walker;

    walker = g_list_next (walker);
    ctts->entries = g_list_remove_link (ctts->entries, aux);
    g_free ((CTTSEntry *) aux->data);
    g_list_free (aux);
  }
  g_free (ctts);
}

static void
atom_stts_init (AtomSTTS * stts)
{
  guint8 flags[3] = { 0, 0, 0 };

  atom_full_init (&stts->header, FOURCC_stts, 0, 0, 0, flags);
  stts->entries = NULL;
}

static void
atom_stts_clear (AtomSTTS * stts)
{
  GList *walker;

  atom_full_clear (&stts->header);
  walker = stts->entries;
  while (walker) {
    GList *aux = walker;

    walker = g_list_next (walker);
    stts->entries = g_list_remove_link (stts->entries, aux);
    g_free ((STTSEntry *) aux->data);
    g_list_free (aux);
  }
  stts->n_entries = 0;
}

static void
atom_stsz_init (AtomSTSZ * stsz)
{
  guint8 flags[3] = { 0, 0, 0 };

  atom_full_init (&stsz->header, FOURCC_stsz, 0, 0, 0, flags);
  stsz->sample_size = 0;
  stsz->table_size = 0;
  stsz->entries = NULL;
}

static void
atom_stsz_clear (AtomSTSZ * stsz)
{
  atom_full_clear (&stsz->header);
  g_list_free (stsz->entries);
  stsz->entries = NULL;
  stsz->table_size = 0;
}

static void
atom_stsc_init (AtomSTSC * stsc)
{
  guint8 flags[3] = { 0, 0, 0 };

  atom_full_init (&stsc->header, FOURCC_stsc, 0, 0, 0, flags);
  stsc->entries = NULL;
  stsc->n_entries = 0;
}

static void
atom_stsc_clear (AtomSTSC * stsc)
{
  GList *walker;

  atom_full_clear (&stsc->header);
  walker = stsc->entries;
  while (walker) {
    GList *aux = walker;

    walker = g_list_next (walker);
    stsc->entries = g_list_remove_link (stsc->entries, aux);
    g_free ((STSCEntry *) aux->data);
    g_list_free (aux);
  }
  stsc->n_entries = 0;
}

static void
atom_co64_init (AtomSTCO64 * co64)
{
  guint8 flags[3] = { 0, 0, 0 };

  atom_full_init (&co64->header, FOURCC_co64, 0, 0, 0, flags);
  co64->entries = NULL;
  co64->n_entries = 0;
}

static void
atom_stco64_clear (AtomSTCO64 * stco64)
{
  GList *walker;

  atom_full_clear (&stco64->header);
  walker = stco64->entries;
  while (walker) {
    GList *aux = walker;

    walker = g_list_next (walker);
    stco64->entries = g_list_remove_link (stco64->entries, aux);
    g_free ((guint64 *) aux->data);
    g_list_free (aux);
  }
  stco64->n_entries = 0;
}

static void
atom_stss_init (AtomSTSS * stss)
{
  guint8 flags[3] = { 0, 0, 0 };

  atom_full_init (&stss->header, FOURCC_stss, 0, 0, 0, flags);
  stss->entries = NULL;
  stss->n_entries = 0;
}

static void
atom_stss_clear (AtomSTSS * stss)
{
  atom_full_clear (&stss->header);
  g_list_free (stss->entries);
  stss->entries = NULL;
  stss->n_entries = 0;
}

static void
atom_stbl_init (AtomSTBL * stbl)
{
  atom_header_set (&stbl->header, FOURCC_stbl, 0, 0);

  atom_stts_init (&stbl->stts);
  atom_stss_init (&stbl->stss);
  atom_stsd_init (&stbl->stsd);
  atom_stsz_init (&stbl->stsz);
  atom_stsc_init (&stbl->stsc);
  stbl->ctts = NULL;

  atom_co64_init (&stbl->stco64);
}

static void
atom_stbl_clear (AtomSTBL * stbl)
{
  atom_clear (&stbl->header);
  atom_stsd_clear (&stbl->stsd);
  atom_stts_clear (&stbl->stts);
  atom_stss_clear (&stbl->stss);
  atom_stsc_clear (&stbl->stsc);
  atom_stsz_clear (&stbl->stsz);
  if (stbl->ctts) {
    atom_ctts_free (stbl->ctts);
  }
  atom_stco64_clear (&stbl->stco64);
}

static void
atom_vmhd_init (AtomVMHD * vmhd, AtomsContext * context)
{
  guint8 flags[3] = { 0, 0, 1 };

  atom_full_init (&vmhd->header, FOURCC_vmhd, 0, 0, 0, flags);
  vmhd->graphics_mode = 0x0;
  memset (vmhd->opcolor, 0, sizeof (guint16) * 3);

  if (context->flavor == ATOMS_TREE_FLAVOR_MOV) {
    vmhd->graphics_mode = 0x40;
    vmhd->opcolor[0] = 32768;
    vmhd->opcolor[1] = 32768;
    vmhd->opcolor[2] = 32768;
  }
}

static AtomVMHD *
atom_vmhd_new (AtomsContext * context)
{
  AtomVMHD *vmhd = g_new0 (AtomVMHD, 1);

  atom_vmhd_init (vmhd, context);
  return vmhd;
}

static void
atom_vmhd_free (AtomVMHD * vmhd)
{
  atom_full_clear (&vmhd->header);
  g_free (vmhd);
}

static void
atom_smhd_init (AtomSMHD * smhd)
{
  guint8 flags[3] = { 0, 0, 0 };

  atom_full_init (&smhd->header, FOURCC_smhd, 0, 0, 0, flags);
  smhd->balance = 0;
  smhd->reserved = 0;
}

static AtomSMHD *
atom_smhd_new ()
{
  AtomSMHD *smhd = g_new0 (AtomSMHD, 1);

  atom_smhd_init (smhd);
  return smhd;
}

static void
atom_smhd_free (AtomSMHD * smhd)
{
  atom_full_clear (&smhd->header);
  g_free (smhd);
}

static void
atom_hmhd_free (AtomHMHD * hmhd)
{
  atom_full_clear (&hmhd->header);
  g_free (hmhd);
}

static void
atom_hdlr_init (AtomHDLR * hdlr)
{
  guint8 flags[3] = { 0, 0, 0 };

  atom_full_init (&hdlr->header, FOURCC_hdlr, 0, 0, 0, flags);

  hdlr->component_type = 0;
  hdlr->handler_type = 0;
  hdlr->manufacturer = 0;
  hdlr->flags = 0;
  hdlr->flags_mask = 0;
  hdlr->name = g_strdup ("");
}

static AtomHDLR *
atom_hdlr_new ()
{
  AtomHDLR *hdlr = g_new0 (AtomHDLR, 1);

  atom_hdlr_init (hdlr);
  return hdlr;
}

static void
atom_hdlr_clear (AtomHDLR * hdlr)
{
  atom_full_clear (&hdlr->header);
  if (hdlr->name) {
    g_free (hdlr->name);
    hdlr->name = NULL;
  }
}

static void
atom_hdlr_free (AtomHDLR * hdlr)
{
  atom_hdlr_clear (hdlr);
  g_free (hdlr);
}

static void
atom_url_init (AtomURL * url)
{
  guint8 flags[3] = { 0, 0, 1 };

  atom_full_init (&url->header, FOURCC_url_, 0, 0, 0, flags);
  url->location = NULL;
}

static void
atom_url_free (AtomURL * url)
{
  atom_full_clear (&url->header);
  if (url->location) {
    g_free (url->location);
    url->location = NULL;
  }
  g_free (url);
}

static AtomURL *
atom_url_new ()
{
  AtomURL *url = g_new0 (AtomURL, 1);

  atom_url_init (url);
  return url;
}

static AtomFull *
atom_alis_new ()
{
  guint8 flags[3] = { 0, 0, 1 };
  AtomFull *alis = g_new0 (AtomFull, 1);

  atom_full_init (alis, FOURCC_alis, 0, 0, 0, flags);
  return alis;
}

static void
atom_dref_init (AtomDREF * dref, AtomsContext * context)
{
  guint8 flags[3] = { 0, 0, 0 };

  atom_full_init (&dref->header, FOURCC_dref, 0, 0, 0, flags);

  /* in either case, alis or url init arranges to set self-contained flag */
  if (context->flavor == ATOMS_TREE_FLAVOR_MOV) {
    /* alis dref for qt */
    AtomFull *alis = atom_alis_new ();
    dref->entries = g_list_append (dref->entries, alis);
  } else {
    /* url for iso spec, as 'alis' not specified there */
    AtomURL *url = atom_url_new ();
    dref->entries = g_list_append (dref->entries, url);
  }
}

static void
atom_dref_clear (AtomDREF * dref)
{
  GList *walker;

  atom_full_clear (&dref->header);
  walker = dref->entries;
  while (walker) {
    GList *aux = walker;
    Atom *atom = (Atom *) aux->data;

    walker = g_list_next (walker);
    dref->entries = g_list_remove_link (dref->entries, aux);
    switch (atom->type) {
      case FOURCC_alis:
        atom_full_free ((AtomFull *) atom);
        break;
      case FOURCC_url_:
        atom_url_free ((AtomURL *) atom);
        break;
      default:
        /* we do nothing, better leak than crash */
        break;
    }
    g_list_free (aux);
  }
}

static void
atom_dinf_init (AtomDINF * dinf, AtomsContext * context)
{
  atom_header_set (&dinf->header, FOURCC_dinf, 0, 0);
  atom_dref_init (&dinf->dref, context);
}

static void
atom_dinf_clear (AtomDINF * dinf)
{
  atom_clear (&dinf->header);
  atom_dref_clear (&dinf->dref);
}

static void
atom_minf_init (AtomMINF * minf, AtomsContext * context)
{
  atom_header_set (&minf->header, FOURCC_minf, 0, 0);

  minf->vmhd = NULL;
  minf->smhd = NULL;
  minf->hmhd = NULL;

  if (context->flavor == ATOMS_TREE_FLAVOR_MOV) {
    minf->hdlr = atom_hdlr_new ();
    minf->hdlr->component_type = FOURCC_dhlr;
    minf->hdlr->handler_type = FOURCC_alis;
  } else {
    minf->hdlr = NULL;
  }
  atom_dinf_init (&minf->dinf, context);
  atom_stbl_init (&minf->stbl);
}

static void
atom_minf_clear_handlers (AtomMINF * minf)
{
  if (minf->vmhd) {
    atom_vmhd_free (minf->vmhd);
    minf->vmhd = NULL;
  }
  if (minf->smhd) {
    atom_smhd_free (minf->smhd);
    minf->smhd = NULL;
  }
  if (minf->hmhd) {
    atom_hmhd_free (minf->hmhd);
    minf->hmhd = NULL;
  }
}

static void
atom_minf_clear (AtomMINF * minf)
{
  atom_clear (&minf->header);
  atom_minf_clear_handlers (minf);
  if (minf->hdlr) {
    atom_hdlr_free (minf->hdlr);
  }
  atom_dinf_clear (&minf->dinf);
  atom_stbl_clear (&minf->stbl);
}

static void
atom_mdhd_init (AtomMDHD * mdhd)
{
  guint8 flags[3] = { 0, 0, 0 };

  atom_full_init (&mdhd->header, FOURCC_mdhd, 0, 0, 0, flags);
  common_time_info_init (&mdhd->time_info);
  mdhd->language_code = 0;
  mdhd->quality = 0;
}

static void
atom_mdhd_clear (AtomMDHD * mdhd)
{
  atom_full_clear (&mdhd->header);
}

static void
atom_mdia_init (AtomMDIA * mdia, AtomsContext * context)
{
  atom_header_set (&mdia->header, FOURCC_mdia, 0, 0);

  atom_mdhd_init (&mdia->mdhd);
  atom_hdlr_init (&mdia->hdlr);
  atom_minf_init (&mdia->minf, context);
}

static void
atom_mdia_clear (AtomMDIA * mdia)
{
  atom_clear (&mdia->header);
  atom_mdhd_clear (&mdia->mdhd);
  atom_hdlr_clear (&mdia->hdlr);
  atom_minf_clear (&mdia->minf);
}

static void
atom_tkhd_init (AtomTKHD * tkhd, AtomsContext * context)
{
  /*
   * flags info
   * 1 -> track enabled
   * 2 -> track in movie
   * 4 -> track in preview
   */
  guint8 flags[3] = { 0, 0, 7 };

  atom_full_init (&tkhd->header, FOURCC_tkhd, 0, 0, 0, flags);

  tkhd->creation_time = tkhd->modification_time = get_current_qt_time ();
  tkhd->duration = 0;
  tkhd->track_ID = 0;
  tkhd->reserved = 0;

  tkhd->reserved2[0] = tkhd->reserved2[1] = 0;
  tkhd->layer = 0;
  tkhd->alternate_group = 0;
  tkhd->volume = 0;
  tkhd->reserved3 = 0;
  memset (tkhd->matrix, 0, sizeof (guint32) * 9);
  tkhd->matrix[0] = 1 << 16;
  tkhd->matrix[4] = 1 << 16;
  tkhd->matrix[8] = 16384 << 16;
  tkhd->width = 0;
  tkhd->height = 0;
}

static void
atom_tkhd_clear (AtomTKHD * tkhd)
{
  atom_full_clear (&tkhd->header);
}

static void
atom_trak_init (AtomTRAK * trak, AtomsContext * context)
{
  atom_header_set (&trak->header, FOURCC_trak, 0, 0);

  atom_tkhd_init (&trak->tkhd, context);
  trak->edts = NULL;
  atom_mdia_init (&trak->mdia, context);
}

AtomTRAK *
atom_trak_new (AtomsContext * context)
{
  AtomTRAK *trak = g_new0 (AtomTRAK, 1);

  atom_trak_init (trak, context);
  return trak;
}

static void
atom_trak_free (AtomTRAK * trak)
{
  atom_clear (&trak->header);
  atom_tkhd_clear (&trak->tkhd);
  if (trak->edts)
    atom_edts_free (trak->edts);
  atom_mdia_clear (&trak->mdia);
  g_free (trak);
}

static void
atom_ilst_init (AtomILST * ilst)
{
  atom_header_set (&ilst->header, FOURCC_ilst, 0, 0);
  ilst->entries = NULL;
}

static AtomILST *
atom_ilst_new ()
{
  AtomILST *ilst = g_new0 (AtomILST, 1);

  atom_ilst_init (ilst);
  return ilst;
}

static void
atom_ilst_free (AtomILST * ilst)
{
  if (ilst->entries)
    atom_info_list_free (ilst->entries);
  atom_clear (&ilst->header);
  g_free (ilst);
}

static void
atom_meta_init (AtomMETA * meta)
{
  guint8 flags[3] = { 0, 0, 0 };

  atom_full_init (&meta->header, FOURCC_meta, 0, 0, 0, flags);
  atom_hdlr_init (&meta->hdlr);
  /* FIXME (ISOM says this is always 0) */
  meta->hdlr.component_type = FOURCC_mhlr;
  meta->hdlr.handler_type = FOURCC_mdir;
  meta->ilst = NULL;
}

static AtomMETA *
atom_meta_new ()
{
  AtomMETA *meta = g_new0 (AtomMETA, 1);

  atom_meta_init (meta);
  return meta;
}

static void
atom_meta_free (AtomMETA * meta)
{
  atom_full_clear (&meta->header);
  atom_hdlr_clear (&meta->hdlr);
  if (meta->ilst)
    atom_ilst_free (meta->ilst);
  meta->ilst = NULL;
  g_free (meta);
}

static void
atom_udta_init (AtomUDTA * udta)
{
  atom_header_set (&udta->header, FOURCC_udta, 0, 0);
  udta->meta = NULL;
}

static AtomUDTA *
atom_udta_new ()
{
  AtomUDTA *udta = g_new0 (AtomUDTA, 1);

  atom_udta_init (udta);
  return udta;
}

static void
atom_udta_free (AtomUDTA * udta)
{
  atom_clear (&udta->header);
  if (udta->meta)
    atom_meta_free (udta->meta);
  udta->meta = NULL;
  if (udta->entries)
    atom_info_list_free (udta->entries);
  g_free (udta);
}

static void
atom_tag_data_init (AtomTagData * data)
{
  guint8 flags[] = { 0, 0, 0 };

  atom_full_init (&data->header, FOURCC_data, 0, 0, 0, flags);
}

static void
atom_tag_data_clear (AtomTagData * data)
{
  atom_full_clear (&data->header);
  g_free (data->data);
  data->datalen = 0;
}

/*
 * Fourcc is the tag fourcc
 * flags will be truncated to 24bits
 */
static AtomTag *
atom_tag_new (guint32 fourcc, guint32 flags_as_uint)
{
  AtomTag *tag = g_new0 (AtomTag, 1);

  tag->header.type = fourcc;
  atom_tag_data_init (&tag->data);
  tag->data.header.flags[2] = flags_as_uint & 0xFF;
  tag->data.header.flags[1] = (flags_as_uint & 0xFF00) >> 8;
  tag->data.header.flags[0] = (flags_as_uint & 0xFF0000) >> 16;
  return tag;
}

static void
atom_tag_free (AtomTag * tag)
{
  atom_clear (&tag->header);
  atom_tag_data_clear (&tag->data);
  g_free (tag);
}

static void
atom_mvhd_init (AtomMVHD * mvhd)
{
  guint8 flags[3] = { 0, 0, 0 };

  atom_full_init (&(mvhd->header), FOURCC_mvhd, sizeof (AtomMVHD), 0, 0, flags);

  common_time_info_init (&mvhd->time_info);

  mvhd->prefered_rate = 1 << 16;
  mvhd->volume = 1 << 8;
  mvhd->reserved3 = 0;
  memset (mvhd->reserved4, 0, sizeof (guint32[2]));

  memset (mvhd->matrix, 0, sizeof (guint32[9]));
  mvhd->matrix[0] = 1 << 16;
  mvhd->matrix[4] = 1 << 16;
  mvhd->matrix[8] = 16384 << 16;

  mvhd->preview_time = 0;
  mvhd->preview_duration = 0;
  mvhd->poster_time = 0;
  mvhd->selection_time = 0;
  mvhd->selection_duration = 0;
  mvhd->current_time = 0;

  mvhd->next_track_id = 1;
}

static void
atom_mvhd_clear (AtomMVHD * mvhd)
{
  atom_full_clear (&mvhd->header);
}

static void
atom_moov_init (AtomMOOV * moov, AtomsContext * context)
{
  atom_header_set (&(moov->header), FOURCC_moov, 0, 0);
  atom_mvhd_init (&(moov->mvhd));
  moov->udta = NULL;
  moov->traks = NULL;
  moov->context = *context;
}

AtomMOOV *
atom_moov_new (AtomsContext * context)
{
  AtomMOOV *moov = g_new0 (AtomMOOV, 1);

  atom_moov_init (moov, context);
  return moov;
}

void
atom_moov_free (AtomMOOV * moov)
{
  GList *walker;

  atom_clear (&moov->header);
  atom_mvhd_clear (&moov->mvhd);

  walker = moov->traks;
  while (walker) {
    atom_trak_free ((AtomTRAK *) walker->data);
    walker = g_list_next (walker);
  }
  g_list_free (moov->traks);
  moov->traks = NULL;

  if (moov->udta) {
    atom_udta_free (moov->udta);
    moov->udta = NULL;
  }

  g_free (moov);
}

/* -- end of init / free -- */

/* -- copy data functions -- */

static guint8
atom_full_get_version (AtomFull * full)
{
  return full->version;
}

static guint64
common_time_info_copy_data (TimeInfo * ti, gboolean trunc_to_32,
    guint8 ** buffer, guint64 * size, guint64 * offset)
{
  guint64 original_offset = *offset;

  if (trunc_to_32) {
    prop_copy_uint32 ((guint32) ti->creation_time, buffer, size, offset);
    prop_copy_uint32 ((guint32) ti->modification_time, buffer, size, offset);
    prop_copy_uint32 (ti->timescale, buffer, size, offset);
    prop_copy_uint32 ((guint32) ti->duration, buffer, size, offset);
  } else {
    prop_copy_uint64 (ti->creation_time, buffer, size, offset);
    prop_copy_uint64 (ti->modification_time, buffer, size, offset);
    prop_copy_uint32 (ti->timescale, buffer, size, offset);
    prop_copy_uint64 (ti->duration, buffer, size, offset);
  }
  return *offset - original_offset;
}

static void
atom_write_size (guint8 ** buffer, guint64 * size, guint64 * offset,
    guint64 atom_pos)
{
  /* this only works for non-extended atom size, which is OK
   * (though it could be made to do mem_move, etc and write extended size) */
  prop_copy_uint32 (*offset - atom_pos, buffer, size, &atom_pos);
}

guint64
atom_copy_data (Atom * atom, guint8 ** buffer, guint64 * size, guint64 * offset)
{
  guint64 original_offset = *offset;

  /* copies type and size */
  prop_copy_uint32 (atom->size, buffer, size, offset);
  prop_copy_fourcc (atom->type, buffer, size, offset);

  /* extended size needed */
  if (atom->size == 1) {
    /* really should not happen other than with mdat atom;
     * would be a problem for size (re)write code, not to mention memory */
    g_return_val_if_fail (atom->type == FOURCC_mdat, 0);
    prop_copy_uint64 (atom->extended_size, buffer, size, offset);
  }

  return *offset - original_offset;
}

static guint64
atom_full_copy_data (AtomFull * atom, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&atom->header, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint8 (atom->version, buffer, size, offset);
  prop_copy_uint8_array (atom->flags, 3, buffer, size, offset);

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_info_list_copy_data (GList * ai, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  while (ai) {
    AtomInfo *info = (AtomInfo *) ai->data;

    if (!info->copy_data_func (info->atom, buffer, size, offset)) {
      return 0;
    }
    ai = g_list_next (ai);
  }

  return *offset - original_offset;
}

static guint64
atom_data_copy_data (AtomData * data, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&data->header, buffer, size, offset)) {
    return 0;
  }
  if (data->datalen)
    prop_copy_uint8_array (data->data, data->datalen, buffer, size, offset);

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

guint64
atom_ftyp_copy_data (AtomFTYP * ftyp, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&ftyp->header, buffer, size, offset)) {
    return 0;
  }
  prop_copy_fourcc (ftyp->major_brand, buffer, size, offset);
  prop_copy_uint32 (ftyp->version, buffer, size, offset);

  prop_copy_fourcc_array (ftyp->compatible_brands, ftyp->compatible_brands_size,
      buffer, size, offset);

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_mvhd_copy_data (AtomMVHD * atom, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint8 version;
  guint64 original_offset = *offset;

  if (!atom_full_copy_data (&(atom->header), buffer, size, offset)) {
    return 0;
  }

  version = atom_full_get_version (&(atom->header));
  if (version == 0) {
    common_time_info_copy_data (&atom->time_info, TRUE, buffer, size, offset);
  } else if (version == 1) {
    common_time_info_copy_data (&atom->time_info, FALSE, buffer, size, offset);
  } else {
    *offset = original_offset;
    return 0;
  }

  prop_copy_uint32 (atom->prefered_rate, buffer, size, offset);
  prop_copy_uint16 (atom->volume, buffer, size, offset);
  prop_copy_uint16 (atom->reserved3, buffer, size, offset);
  prop_copy_uint32_array (atom->reserved4, 2, buffer, size, offset);
  prop_copy_uint32_array (atom->matrix, 9, buffer, size, offset);
  prop_copy_uint32 (atom->preview_time, buffer, size, offset);
  prop_copy_uint32 (atom->preview_duration, buffer, size, offset);
  prop_copy_uint32 (atom->poster_time, buffer, size, offset);
  prop_copy_uint32 (atom->selection_time, buffer, size, offset);
  prop_copy_uint32 (atom->selection_duration, buffer, size, offset);
  prop_copy_uint32 (atom->current_time, buffer, size, offset);

  prop_copy_uint32 (atom->next_track_id, buffer, size, offset);

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_tkhd_copy_data (AtomTKHD * tkhd, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_full_copy_data (&tkhd->header, buffer, size, offset)) {
    return 0;
  }

  if (atom_full_get_version (&tkhd->header) == 0) {
    prop_copy_uint32 ((guint32) tkhd->creation_time, buffer, size, offset);
    prop_copy_uint32 ((guint32) tkhd->modification_time, buffer, size, offset);
    prop_copy_uint32 (tkhd->track_ID, buffer, size, offset);
    prop_copy_uint32 (tkhd->reserved, buffer, size, offset);
    prop_copy_uint32 ((guint32) tkhd->duration, buffer, size, offset);
  } else {
    prop_copy_uint64 (tkhd->creation_time, buffer, size, offset);
    prop_copy_uint64 (tkhd->modification_time, buffer, size, offset);
    prop_copy_uint32 (tkhd->track_ID, buffer, size, offset);
    prop_copy_uint32 (tkhd->reserved, buffer, size, offset);
    prop_copy_uint64 (tkhd->duration, buffer, size, offset);
  }

  prop_copy_uint32_array (tkhd->reserved2, 2, buffer, size, offset);
  prop_copy_uint16 (tkhd->layer, buffer, size, offset);
  prop_copy_uint16 (tkhd->alternate_group, buffer, size, offset);
  prop_copy_uint16 (tkhd->volume, buffer, size, offset);
  prop_copy_uint16 (tkhd->reserved3, buffer, size, offset);
  prop_copy_uint32_array (tkhd->matrix, 9, buffer, size, offset);

  prop_copy_uint32 (tkhd->width, buffer, size, offset);
  prop_copy_uint32 (tkhd->height, buffer, size, offset);

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_hdlr_copy_data (AtomHDLR * hdlr, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_full_copy_data (&hdlr->header, buffer, size, offset)) {
    return 0;
  }

  prop_copy_fourcc (hdlr->component_type, buffer, size, offset);
  prop_copy_fourcc (hdlr->handler_type, buffer, size, offset);
  prop_copy_fourcc (hdlr->manufacturer, buffer, size, offset);
  prop_copy_uint32 (hdlr->flags, buffer, size, offset);
  prop_copy_uint32 (hdlr->flags_mask, buffer, size, offset);

  prop_copy_null_terminated_string (hdlr->name, buffer, size, offset);

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_vmhd_copy_data (AtomVMHD * vmhd, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_full_copy_data (&vmhd->header, buffer, size, offset)) {
    return 0;
  }
  prop_copy_uint16 (vmhd->graphics_mode, buffer, size, offset);
  prop_copy_uint16_array (vmhd->opcolor, 3, buffer, size, offset);

  atom_write_size (buffer, size, offset, original_offset);
  return original_offset - *offset;
}

static guint64
atom_smhd_copy_data (AtomSMHD * smhd, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_full_copy_data (&smhd->header, buffer, size, offset)) {
    return 0;
  }
  prop_copy_uint16 (smhd->balance, buffer, size, offset);
  prop_copy_uint16 (smhd->reserved, buffer, size, offset);

  atom_write_size (buffer, size, offset, original_offset);
  return original_offset - *offset;
}

static guint64
atom_hmhd_copy_data (AtomHMHD * hmhd, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_full_copy_data (&hmhd->header, buffer, size, offset)) {
    return 0;
  }
  prop_copy_uint16 (hmhd->max_pdu_size, buffer, size, offset);
  prop_copy_uint16 (hmhd->avg_pdu_size, buffer, size, offset);
  prop_copy_uint32 (hmhd->max_bitrate, buffer, size, offset);
  prop_copy_uint32 (hmhd->avg_bitrate, buffer, size, offset);
  prop_copy_uint32 (hmhd->sliding_avg_bitrate, buffer, size, offset);

  atom_write_size (buffer, size, offset, original_offset);
  return original_offset - *offset;
}

static gboolean
atom_url_same_file_flag (AtomURL * url)
{
  return (url->header.flags[2] & 0x1) == 1;
}

static guint64
atom_url_copy_data (AtomURL * url, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_full_copy_data (&url->header, buffer, size, offset)) {
    return 0;
  }

  if (!atom_url_same_file_flag (url)) {
    prop_copy_null_terminated_string (url->location, buffer, size, offset);
  }

  atom_write_size (buffer, size, offset, original_offset);
  return original_offset - *offset;
}

static guint64
atom_stts_copy_data (AtomSTTS * stts, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;
  GList *walker;

  if (!atom_full_copy_data (&stts->header, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint32 (stts->n_entries, buffer, size, offset);
  /* minimize realloc */
  prop_copy_ensure_buffer (buffer, size, offset, 8 * stts->n_entries);
  for (walker = g_list_last (stts->entries); walker != NULL;
      walker = g_list_previous (walker)) {
    STTSEntry *entry = (STTSEntry *) walker->data;

    prop_copy_uint32 (entry->sample_count, buffer, size, offset);
    prop_copy_int32 (entry->sample_delta, buffer, size, offset);
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_sample_entry_copy_data (SampleTableEntry * se, guint8 ** buffer,
    guint64 * size, guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&se->header, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint8_array (se->reserved, 6, buffer, size, offset);
  prop_copy_uint16 (se->data_reference_index, buffer, size, offset);

  return *offset - original_offset;
}

static guint64
atom_esds_copy_data (AtomESDS * esds, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_full_copy_data (&esds->header, buffer, size, offset)) {
    return 0;
  }
  if (!desc_es_descriptor_copy_data (&esds->es, buffer, size, offset)) {
    return 0;
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_frma_copy_data (AtomFRMA * frma, guint8 ** buffer,
    guint64 * size, guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&(frma->header), buffer, size, offset))
    return 0;

  prop_copy_fourcc (frma->media_type, buffer, size, offset);

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_mp4s_copy_data (SampleTableEntryMP4S * mp4s, guint8 ** buffer,
    guint64 * size, guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_sample_entry_copy_data (&mp4s->se, buffer, size, offset)) {
    return 0;
  }
  if (!atom_esds_copy_data (&mp4s->es, buffer, size, offset)) {
    return 0;
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_hint_sample_entry_copy_data (AtomHintSampleEntry * hse, guint8 ** buffer,
    guint64 * size, guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_sample_entry_copy_data (&hse->se, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint32 (hse->size, buffer, size, offset);
  prop_copy_uint8_array (hse->data, hse->size, buffer, size, offset);

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
sample_entry_mp4a_copy_data (SampleTableEntryMP4A * mp4a, guint8 ** buffer,
    guint64 * size, guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_sample_entry_copy_data (&mp4a->se, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint16 (mp4a->version, buffer, size, offset);
  prop_copy_uint16 (mp4a->revision_level, buffer, size, offset);
  prop_copy_uint32 (mp4a->vendor, buffer, size, offset);
  prop_copy_uint16 (mp4a->channels, buffer, size, offset);
  prop_copy_uint16 (mp4a->sample_size, buffer, size, offset);
  prop_copy_uint16 (mp4a->compression_id, buffer, size, offset);
  prop_copy_uint16 (mp4a->packet_size, buffer, size, offset);
  prop_copy_uint32 (mp4a->sample_rate, buffer, size, offset);

  /* this should always be 0 for mp4 flavor */
  if (mp4a->version == 1) {
    prop_copy_uint32 (mp4a->samples_per_packet, buffer, size, offset);
    prop_copy_uint32 (mp4a->bytes_per_packet, buffer, size, offset);
    prop_copy_uint32 (mp4a->bytes_per_frame, buffer, size, offset);
    prop_copy_uint32 (mp4a->bytes_per_sample, buffer, size, offset);
  }

  if (mp4a->extension_atoms) {
    if (!atom_info_list_copy_data (mp4a->extension_atoms, buffer, size, offset))
      return 0;
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
sample_entry_mp4v_copy_data (SampleTableEntryMP4V * mp4v, guint8 ** buffer,
    guint64 * size, guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_sample_entry_copy_data (&mp4v->se, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint16 (mp4v->version, buffer, size, offset);
  prop_copy_uint16 (mp4v->revision_level, buffer, size, offset);
  prop_copy_fourcc (mp4v->vendor, buffer, size, offset);
  prop_copy_uint32 (mp4v->temporal_quality, buffer, size, offset);
  prop_copy_uint32 (mp4v->spatial_quality, buffer, size, offset);

  prop_copy_uint16 (mp4v->width, buffer, size, offset);
  prop_copy_uint16 (mp4v->height, buffer, size, offset);

  prop_copy_uint32 (mp4v->horizontal_resolution, buffer, size, offset);
  prop_copy_uint32 (mp4v->vertical_resolution, buffer, size, offset);
  prop_copy_uint32 (mp4v->datasize, buffer, size, offset);

  prop_copy_uint16 (mp4v->frame_count, buffer, size, offset);

  prop_copy_fixed_size_string ((guint8 *) mp4v->compressor, 32, buffer, size,
      offset);

  prop_copy_uint16 (mp4v->depth, buffer, size, offset);
  prop_copy_uint16 (mp4v->color_table_id, buffer, size, offset);

  /* extra atoms */
  if (mp4v->extension_atoms &&
      !atom_info_list_copy_data (mp4v->extension_atoms, buffer, size, offset))
    return 0;

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_stsz_copy_data (AtomSTSZ * stsz, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;
  GList *walker;

  if (!atom_full_copy_data (&stsz->header, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint32 (stsz->sample_size, buffer, size, offset);
  prop_copy_uint32 (stsz->table_size, buffer, size, offset);
  /* minimize realloc */
  prop_copy_ensure_buffer (buffer, size, offset, 4 * stsz->table_size);
  if (stsz->sample_size == 0) {
    for (walker = g_list_last (stsz->entries); walker != NULL;
        walker = g_list_previous (walker)) {
      prop_copy_uint32 ((guint32) GPOINTER_TO_UINT (walker->data), buffer, size,
          offset);
    }
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_stsc_copy_data (AtomSTSC * stsc, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;
  GList *walker;

  if (!atom_full_copy_data (&stsc->header, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint32 (stsc->n_entries, buffer, size, offset);
  /* minimize realloc */
  prop_copy_ensure_buffer (buffer, size, offset, 12 * stsc->n_entries);

  for (walker = g_list_last (stsc->entries); walker != NULL;
      walker = g_list_previous (walker)) {
    STSCEntry *entry = (STSCEntry *) walker->data;

    prop_copy_uint32 (entry->first_chunk, buffer, size, offset);
    prop_copy_uint32 (entry->samples_per_chunk, buffer, size, offset);
    prop_copy_uint32 (entry->sample_description_index, buffer, size, offset);
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_ctts_copy_data (AtomCTTS * ctts, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;
  GList *walker;

  if (!atom_full_copy_data (&ctts->header, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint32 (ctts->n_entries, buffer, size, offset);
  /* minimize realloc */
  prop_copy_ensure_buffer (buffer, size, offset, 8 * ctts->n_entries);
  for (walker = g_list_last (ctts->entries); walker != NULL;
      walker = g_list_previous (walker)) {
    CTTSEntry *entry = (CTTSEntry *) walker->data;

    prop_copy_uint32 (entry->samplecount, buffer, size, offset);
    prop_copy_uint32 (entry->sampleoffset, buffer, size, offset);
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_stco64_copy_data (AtomSTCO64 * stco64, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;
  GList *walker;
  gboolean trunc_to_32 = stco64->header.header.type == FOURCC_stco;

  if (!atom_full_copy_data (&stco64->header, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint32 (stco64->n_entries, buffer, size, offset);

  /* minimize realloc */
  prop_copy_ensure_buffer (buffer, size, offset, 8 * stco64->n_entries);
  for (walker = g_list_last (stco64->entries); walker != NULL;
      walker = g_list_previous (walker)) {
    guint64 *value = (guint64 *) walker->data;

    if (trunc_to_32) {
      prop_copy_uint32 ((guint32) * value, buffer, size, offset);
    } else {
      prop_copy_uint64 (*value, buffer, size, offset);
    }
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_stss_copy_data (AtomSTSS * stss, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;
  GList *walker;

  if (stss->entries == NULL) {
    /* FIXME not needing this atom might be confused with error while copying */
    return 0;
  }

  if (!atom_full_copy_data (&stss->header, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint32 (stss->n_entries, buffer, size, offset);
  /* minimize realloc */
  prop_copy_ensure_buffer (buffer, size, offset, 4 * stss->n_entries);
  for (walker = g_list_last (stss->entries); walker != NULL;
      walker = g_list_previous (walker)) {
    prop_copy_uint32 ((guint32) GPOINTER_TO_UINT (walker->data), buffer, size,
        offset);
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_stsd_copy_data (AtomSTSD * stsd, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;
  GList *walker;

  if (!atom_full_copy_data (&stsd->header, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint32 (stsd->n_entries, buffer, size, offset);

  for (walker = g_list_last (stsd->entries); walker != NULL;
      walker = g_list_previous (walker)) {
    SampleTableEntry *se = (SampleTableEntry *) walker->data;

    switch (((Atom *) walker->data)->type) {
      case FOURCC_mp4a:
        if (!sample_entry_mp4a_copy_data ((SampleTableEntryMP4A *) walker->data,
                buffer, size, offset)) {
          return 0;
        }
        break;
      case FOURCC_mp4s:
        if (!atom_mp4s_copy_data ((SampleTableEntryMP4S *) walker->data,
                buffer, size, offset)) {
          return 0;
        }
        break;
      case FOURCC_mp4v:
        if (!sample_entry_mp4v_copy_data ((SampleTableEntryMP4V *) walker->data,
                buffer, size, offset)) {
          return 0;
        }
        break;
      default:
        if (se->kind == VIDEO) {
          if (!sample_entry_mp4v_copy_data ((SampleTableEntryMP4V *)
                  walker->data, buffer, size, offset)) {
            return 0;
          }
        } else if (se->kind == AUDIO) {
          if (!sample_entry_mp4a_copy_data ((SampleTableEntryMP4A *)
                  walker->data, buffer, size, offset)) {
            return 0;
          }
        } else {
          if (!atom_hint_sample_entry_copy_data (
                  (AtomHintSampleEntry *) walker->data, buffer, size, offset)) {
            return 0;
          }
        }
        break;
    }
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_stbl_copy_data (AtomSTBL * stbl, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&stbl->header, buffer, size, offset)) {
    return 0;
  }

  if (!atom_stsd_copy_data (&stbl->stsd, buffer, size, offset)) {
    return 0;
  }
  if (!atom_stts_copy_data (&stbl->stts, buffer, size, offset)) {
    return 0;
  }
  /* this atom is optional, so let's check if we need it
   * (to avoid false error) */
  if (stbl->stss.entries) {
    if (!atom_stss_copy_data (&stbl->stss, buffer, size, offset)) {
      return 0;
    }
  }

  if (!atom_stsc_copy_data (&stbl->stsc, buffer, size, offset)) {
    return 0;
  }
  if (!atom_stsz_copy_data (&stbl->stsz, buffer, size, offset)) {
    return 0;
  }
  if (stbl->ctts) {
    if (!atom_ctts_copy_data (stbl->ctts, buffer, size, offset)) {
      return 0;
    }
  }
  if (!atom_stco64_copy_data (&stbl->stco64, buffer, size, offset)) {
    return 0;
  }

  atom_write_size (buffer, size, offset, original_offset);
  return original_offset - *offset;
}


static guint64
atom_dref_copy_data (AtomDREF * dref, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;
  GList *walker;

  if (!atom_full_copy_data (&dref->header, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint32 (g_list_length (dref->entries), buffer, size, offset);

  walker = dref->entries;
  while (walker != NULL) {
    Atom *atom = (Atom *) walker->data;

    if (atom->type == FOURCC_url_) {
      atom_url_copy_data ((AtomURL *) atom, buffer, size, offset);
    } else if (atom->type == FOURCC_alis) {
      atom_full_copy_data ((AtomFull *) atom, buffer, size, offset);
    } else {
      g_error ("Unsupported atom used inside dref atom");
    }
    walker = g_list_next (walker);
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_dinf_copy_data (AtomDINF * dinf, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&dinf->header, buffer, size, offset)) {
    return 0;
  }

  if (!atom_dref_copy_data (&dinf->dref, buffer, size, offset)) {
    return 0;
  }

  atom_write_size (buffer, size, offset, original_offset);
  return original_offset - *offset;
}

static guint64
atom_minf_copy_data (AtomMINF * minf, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&minf->header, buffer, size, offset)) {
    return 0;
  }

  if (minf->vmhd) {
    if (!atom_vmhd_copy_data (minf->vmhd, buffer, size, offset)) {
      return 0;
    }
  } else if (minf->smhd) {
    if (!atom_smhd_copy_data (minf->smhd, buffer, size, offset)) {
      return 0;
    }
  } else if (minf->hmhd) {
    if (!atom_hmhd_copy_data (minf->hmhd, buffer, size, offset)) {
      return 0;
    }
  }

  if (minf->hdlr) {
    if (!atom_hdlr_copy_data (minf->hdlr, buffer, size, offset)) {
      return 0;
    }
  }

  if (!atom_dinf_copy_data (&minf->dinf, buffer, size, offset)) {
    return 0;
  }
  if (!atom_stbl_copy_data (&minf->stbl, buffer, size, offset)) {
    return 0;
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_mdhd_copy_data (AtomMDHD * mdhd, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_full_copy_data (&mdhd->header, buffer, size, offset)) {
    return 0;
  }

  if (!common_time_info_copy_data (&mdhd->time_info,
          atom_full_get_version (&mdhd->header) == 0, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint16 (mdhd->language_code, buffer, size, offset);
  prop_copy_uint16 (mdhd->quality, buffer, size, offset);

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_mdia_copy_data (AtomMDIA * mdia, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&mdia->header, buffer, size, offset)) {
    return 0;
  }
  if (!atom_mdhd_copy_data (&mdia->mdhd, buffer, size, offset)) {
    return 0;
  }
  if (!atom_hdlr_copy_data (&mdia->hdlr, buffer, size, offset)) {
    return 0;
  }

  if (!atom_minf_copy_data (&mdia->minf, buffer, size, offset)) {
    return 0;
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_elst_copy_data (AtomELST * elst, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;
  GSList *walker;

  if (!atom_full_copy_data (&elst->header, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint32 (g_slist_length (elst->entries), buffer, size, offset);

  for (walker = elst->entries; walker != NULL; walker = g_slist_next (walker)) {
    EditListEntry *entry = (EditListEntry *) walker->data;
    prop_copy_uint32 (entry->duration, buffer, size, offset);
    prop_copy_uint32 (entry->media_time, buffer, size, offset);
    prop_copy_uint32 (entry->media_rate, buffer, size, offset);
  }
  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_edts_copy_data (AtomEDTS * edts, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&(edts->header), buffer, size, offset))
    return 0;

  if (!atom_elst_copy_data (&(edts->elst), buffer, size, offset))
    return 0;

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_trak_copy_data (AtomTRAK * trak, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&trak->header, buffer, size, offset)) {
    return 0;
  }
  if (!atom_tkhd_copy_data (&trak->tkhd, buffer, size, offset)) {
    return 0;
  }
  if (trak->edts) {
    if (!atom_edts_copy_data (trak->edts, buffer, size, offset)) {
      return 0;
    }
  }

  if (!atom_mdia_copy_data (&trak->mdia, buffer, size, offset)) {
    return 0;
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_tag_data_copy_data (AtomTagData * data, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_full_copy_data (&data->header, buffer, size, offset)) {
    return 0;
  }

  prop_copy_uint32 (data->reserved, buffer, size, offset);
  prop_copy_uint8_array (data->data, data->datalen, buffer, size, offset);

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_tag_copy_data (AtomTag * tag, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&tag->header, buffer, size, offset)) {
    return 0;
  }

  if (!atom_tag_data_copy_data (&tag->data, buffer, size, offset)) {
    return 0;
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_ilst_copy_data (AtomILST * ilst, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&ilst->header, buffer, size, offset)) {
    return 0;
  }
  /* extra atoms */
  if (ilst->entries &&
      !atom_info_list_copy_data (ilst->entries, buffer, size, offset))
    return 0;

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_meta_copy_data (AtomMETA * meta, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_full_copy_data (&meta->header, buffer, size, offset)) {
    return 0;
  }
  if (!atom_hdlr_copy_data (&meta->hdlr, buffer, size, offset)) {
    return 0;
  }
  if (meta->ilst) {
    if (!atom_ilst_copy_data (meta->ilst, buffer, size, offset)) {
      return 0;
    }
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_udta_copy_data (AtomUDTA * udta, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&udta->header, buffer, size, offset)) {
    return 0;
  }
  if (udta->meta) {
    if (!atom_meta_copy_data (udta->meta, buffer, size, offset)) {
      return 0;
    }
  } else if (udta->entries) {
    /* extra atoms */
    if (!atom_info_list_copy_data (udta->entries, buffer, size, offset))
      return 0;
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

guint64
atom_moov_copy_data (AtomMOOV * atom, guint8 ** buffer, guint64 * size,
    guint64 * offset)
{
  guint64 original_offset = *offset;
  GList *walker;

  if (!atom_copy_data (&(atom->header), buffer, size, offset))
    return 0;

  if (!atom_mvhd_copy_data (&(atom->mvhd), buffer, size, offset))
    return 0;

  walker = g_list_first (atom->traks);
  while (walker != NULL) {
    if (!atom_trak_copy_data ((AtomTRAK *) walker->data, buffer, size, offset)) {
      return 0;
    }
    walker = g_list_next (walker);
  }

  if (atom->udta) {
    if (!atom_udta_copy_data (atom->udta, buffer, size, offset)) {
      return 0;
    }
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

static guint64
atom_wave_copy_data (AtomWAVE * wave, guint8 ** buffer,
    guint64 * size, guint64 * offset)
{
  guint64 original_offset = *offset;

  if (!atom_copy_data (&(wave->header), buffer, size, offset))
    return 0;

  if (wave->extension_atoms) {
    if (!atom_info_list_copy_data (wave->extension_atoms, buffer, size, offset))
      return 0;
  }

  atom_write_size (buffer, size, offset, original_offset);
  return *offset - original_offset;
}

/* -- end of copy data functions -- */

/* -- general functions, API and support functions */

/* add samples to tables */

static STSCEntry *
stsc_entry_new (guint32 first_chunk, guint32 samples, guint32 desc_index)
{
  STSCEntry *entry = g_new0 (STSCEntry, 1);

  entry->first_chunk = first_chunk;
  entry->samples_per_chunk = samples;
  entry->sample_description_index = desc_index;
  return entry;
}

static void
atom_stsc_add_new_entry (AtomSTSC * stsc, guint32 first_chunk, guint32 nsamples)
{
  if (stsc->entries &&
      ((STSCEntry *) stsc->entries->data)->samples_per_chunk == nsamples)
    return;

  stsc->entries =
      g_list_prepend (stsc->entries, stsc_entry_new (first_chunk, nsamples, 1));
  stsc->n_entries++;
}

static STTSEntry *
stts_entry_new (guint32 sample_count, gint32 sample_delta)
{
  STTSEntry *entry = g_new0 (STTSEntry, 1);

  entry->sample_count = sample_count;
  entry->sample_delta = sample_delta;
  return entry;
}

static void
atom_stts_add_entry (AtomSTTS * stts, guint32 sample_count, gint32 sample_delta)
{
  STTSEntry *entry;

  if (stts->entries == NULL) {
    stts->entries =
        g_list_prepend (stts->entries, stts_entry_new (sample_count,
            sample_delta));
    stts->n_entries++;
    return;
  }
  entry = (STTSEntry *) g_list_first (stts->entries)->data;
  if (entry->sample_delta == sample_delta) {
    entry->sample_count += sample_count;
  } else {
    stts->entries =
        g_list_prepend (stts->entries, stts_entry_new (sample_count,
            sample_delta));
    stts->n_entries++;
  }
}

static void
atom_stsz_add_entry (AtomSTSZ * stsz, guint32 nsamples, guint32 size)
{
  guint32 i;

  stsz->table_size += nsamples;
  if (stsz->sample_size != 0) {
    /* it is constant size, we don't need entries */
    return;
  }
  for (i = 0; i < nsamples; i++) {
    stsz->entries = g_list_prepend (stsz->entries, GUINT_TO_POINTER (size));
  }
}

static guint32
atom_stco64_get_entry_count (AtomSTCO64 * stco64)
{
  return stco64->n_entries;
}

static void
atom_stco64_add_entry (AtomSTCO64 * stco64, guint64 entry)
{
  guint64 *pont = g_new (guint64, 1);

  *pont = entry;
  stco64->entries = g_list_prepend (stco64->entries, pont);
  stco64->n_entries++;
}

static void
atom_stss_add_entry (AtomSTSS * stss, guint32 sample)
{
  stss->entries = g_list_prepend (stss->entries, GUINT_TO_POINTER (sample));
  stss->n_entries++;
}

static void
atom_stbl_add_stss_entry (AtomSTBL * stbl)
{
  guint32 sample_index = stbl->stsz.table_size;

  atom_stss_add_entry (&stbl->stss, sample_index);
}

static void
atom_ctts_add_entry (AtomCTTS * ctts, guint32 nsamples, guint32 offset)
{
  GList *walker;
  CTTSEntry *entry;

  walker = g_list_first (ctts->entries);
  entry = (walker == NULL) ? NULL : (CTTSEntry *) walker->data;

  if (entry == NULL || entry->sampleoffset != offset) {
    CTTSEntry *entry = g_new0 (CTTSEntry, 1);

    entry->samplecount = nsamples;
    entry->sampleoffset = offset;
    ctts->entries = g_list_prepend (ctts->entries, entry);
    ctts->n_entries++;
  } else {
    entry->samplecount += nsamples;
  }
}

static void
atom_stbl_add_ctts_entry (AtomSTBL * stbl, guint32 nsamples, guint32 offset)
{
  if (stbl->ctts == NULL) {
    stbl->ctts = atom_ctts_new ();
  }
  atom_ctts_add_entry (stbl->ctts, nsamples, offset);
}

void
atom_trak_add_samples (AtomTRAK * trak, guint32 nsamples, guint32 delta,
    guint32 size, guint64 chunk_offset, gboolean sync,
    gboolean do_pts, gint64 pts_offset)
{
  AtomSTBL *stbl = &trak->mdia.minf.stbl;

  atom_stts_add_entry (&stbl->stts, nsamples, delta);
  atom_stsz_add_entry (&stbl->stsz, nsamples, size);
  atom_stco64_add_entry (&stbl->stco64, chunk_offset);
  atom_stsc_add_new_entry (&stbl->stsc,
      atom_stco64_get_entry_count (&stbl->stco64), nsamples);
  if (sync)
    atom_stbl_add_stss_entry (stbl);
  if (do_pts)
    atom_stbl_add_ctts_entry (stbl, nsamples, pts_offset);
}

/* trak and moov molding */

guint32
atom_trak_get_timescale (AtomTRAK * trak)
{
  return trak->mdia.mdhd.time_info.timescale;
}

static void
atom_trak_set_id (AtomTRAK * trak, guint32 id)
{
  trak->tkhd.track_ID = id;
}

void
atom_moov_add_trak (AtomMOOV * moov, AtomTRAK * trak)
{
  atom_trak_set_id (trak, moov->mvhd.next_track_id++);
  moov->traks = g_list_append (moov->traks, trak);
}

static guint64
atom_trak_get_duration (AtomTRAK * trak)
{
  return trak->tkhd.duration;
}

static guint64
atom_stts_get_total_duration (AtomSTTS * stts)
{
  GList *walker = stts->entries;
  guint64 sum = 0;

  while (walker) {
    STTSEntry *entry = (STTSEntry *) walker->data;

    sum += (guint64) (entry->sample_count) * entry->sample_delta;
    walker = g_list_next (walker);
  }
  return sum;
}

static void
atom_trak_update_duration (AtomTRAK * trak, guint64 moov_timescale)
{
  trak->mdia.mdhd.time_info.duration =
      atom_stts_get_total_duration (&trak->mdia.minf.stbl.stts);
  if (trak->mdia.mdhd.time_info.timescale != 0) {
    trak->tkhd.duration =
        gst_util_uint64_scale (trak->mdia.mdhd.time_info.duration,
        moov_timescale, trak->mdia.mdhd.time_info.timescale);
  } else {
    trak->tkhd.duration = 0;
  }
}

static guint32
atom_moov_get_timescale (AtomMOOV * moov)
{
  return moov->mvhd.time_info.timescale;
}

void
atom_moov_update_timescale (AtomMOOV * moov, guint32 timescale)
{
  moov->mvhd.time_info.timescale = timescale;
}

void
atom_moov_update_duration (AtomMOOV * moov)
{
  GList *traks = moov->traks;
  guint64 dur, duration = 0;

  while (traks) {
    AtomTRAK *trak = (AtomTRAK *) traks->data;

    atom_trak_update_duration (trak, atom_moov_get_timescale (moov));
    dur = atom_trak_get_duration (trak);
    if (dur > duration)
      duration = dur;
    traks = g_list_next (traks);
  }
  moov->mvhd.time_info.duration = duration;
}

static void
atom_set_type (Atom * atom, guint32 fourcc)
{
  atom->type = fourcc;
}

static void
atom_stbl_set_64bits (AtomSTBL * stbl, gboolean use)
{
  if (use) {
    atom_set_type (&stbl->stco64.header.header, FOURCC_co64);
  } else {
    atom_set_type (&stbl->stco64.header.header, FOURCC_stco);
  }
}

static void
atom_trak_set_64bits (AtomTRAK * trak, gboolean use)
{
  atom_stbl_set_64bits (&trak->mdia.minf.stbl, use);
}

void
atom_moov_set_64bits (AtomMOOV * moov, gboolean large_file)
{
  GList *traks = moov->traks;

  while (traks) {
    AtomTRAK *trak = (AtomTRAK *) traks->data;

    atom_trak_set_64bits (trak, large_file);
    traks = g_list_next (traks);
  }
}

static void
atom_stco64_chunks_add_offset (AtomSTCO64 * stco64, guint32 offset)
{
  GList *entries = stco64->entries;

  while (entries) {
    guint64 *value = (guint64 *) entries->data;

    *value += offset;
    entries = g_list_next (entries);
  }
}

void
atom_moov_chunks_add_offset (AtomMOOV * moov, guint32 offset)
{
  GList *traks = moov->traks;

  while (traks) {
    AtomTRAK *trak = (AtomTRAK *) traks->data;

    atom_stco64_chunks_add_offset (&trak->mdia.minf.stbl.stco64, offset);
    traks = g_list_next (traks);
  }
}

/*
 * Meta tags functions
 */
static void
atom_moov_init_metatags (AtomMOOV * moov, AtomsContext * context)
{
  if (!moov->udta) {
    moov->udta = atom_udta_new ();
  }
  if (context->flavor != ATOMS_TREE_FLAVOR_3GP) {
    if (!moov->udta->meta) {
      moov->udta->meta = atom_meta_new ();
    }
    if (!moov->udta->meta->ilst) {
      moov->udta->meta->ilst = atom_ilst_new ();
    }
  }
}

static void
atom_tag_data_alloc_data (AtomTagData * data, guint size)
{
  if (data->data != NULL) {
    g_free (data->data);
  }
  data->data = g_new0 (guint8, size);
  data->datalen = size;
}

static void
atom_moov_append_tag (AtomMOOV * moov, AtomInfo * tag)
{
  GList **entries;

  atom_moov_init_metatags (moov, &moov->context);
  if (moov->udta->meta)
    entries = &moov->udta->meta->ilst->entries;
  else
    entries = &moov->udta->entries;
  *entries = g_list_append (*entries, tag);
}

void
atom_moov_add_tag (AtomMOOV * moov, guint32 fourcc, guint32 flags,
    const guint8 * data, guint size)
{
  AtomTag *tag;
  AtomTagData *tdata;

  tag = atom_tag_new (fourcc, flags);
  tdata = &tag->data;
  atom_tag_data_alloc_data (tdata, size);
  g_memmove (tdata->data, data, size);

  atom_moov_append_tag (moov,
      build_atom_info_wrapper ((Atom *) tag, atom_tag_copy_data,
          atom_tag_free));
}

void
atom_moov_add_str_tag (AtomMOOV * moov, guint32 fourcc, const gchar * value)
{
  gint len = strlen (value);

  if (len > 0)
    atom_moov_add_tag (moov, fourcc, METADATA_TEXT_FLAG, (guint8 *) value, len);
}

void
atom_moov_add_uint_tag (AtomMOOV * moov, guint32 fourcc, guint32 flags,
    guint32 value)
{
  guint8 data[8] = { 0, };

  if (flags) {
    GST_WRITE_UINT16_BE (data, value);
    atom_moov_add_tag (moov, fourcc, flags, data, 2);
  } else {
    GST_WRITE_UINT32_BE (data + 2, value);
    atom_moov_add_tag (moov, fourcc, flags, data, 8);
  }
}

void
atom_moov_add_blob_tag (AtomMOOV * moov, guint8 * data, guint size)
{
  AtomData *data_atom;
  GstBuffer *buf;
  guint len;
  guint32 fourcc;

  if (size < 8)
    return;

  /* blob is unparsed atom;
   * extract size and fourcc, and wrap remainder in data atom */
  len = GST_READ_UINT32_BE (data);
  fourcc = GST_READ_UINT32_LE (data + 4);
  if (len > size)
    return;

  buf = gst_buffer_new ();
  GST_BUFFER_SIZE (buf) = len - 8;
  GST_BUFFER_DATA (buf) = data + 8;

  data_atom = atom_data_new_from_gst_buffer (fourcc, buf);
  gst_buffer_unref (buf);

  atom_moov_append_tag (moov,
      build_atom_info_wrapper ((Atom *) data_atom, atom_data_copy_data,
          atom_data_free));
}

void
atom_moov_add_3gp_tag (AtomMOOV * moov, guint32 fourcc, guint8 * data,
    guint size)
{
  AtomData *data_atom;
  GstBuffer *buf;
  guint8 *bdata;

  /* need full atom */
  buf = gst_buffer_new_and_alloc (size + 4);
  bdata = GST_BUFFER_DATA (buf);
  /* full atom: version and flags */
  GST_WRITE_UINT32_BE (bdata, 0);
  memcpy (bdata + 4, data, size);

  data_atom = atom_data_new_from_gst_buffer (fourcc, buf);
  gst_buffer_unref (buf);

  atom_moov_append_tag (moov,
      build_atom_info_wrapper ((Atom *) data_atom, atom_data_copy_data,
          atom_data_free));
}

guint16
language_code (const char *lang)
{
  g_return_val_if_fail (lang != NULL, 0);
  g_return_val_if_fail (strlen (lang) == 3, 0);

  return (((lang[0] - 0x60) & 0x1F) << 10) + (((lang[1] - 0x60) & 0x1F) << 5) +
      ((lang[2] - 0x60) & 0x1F);
}

void
atom_moov_add_3gp_str_int_tag (AtomMOOV * moov, guint32 fourcc,
    const gchar * value, gint16 ivalue)
{
  gint len = 0, size = 0;
  guint8 *data;

  if (value) {
    len = strlen (value);
    size = len + 3;
  }

  if (ivalue >= 0)
    size += 2;

  data = g_malloc (size + 3);
  /* language tag and null-terminated UTF-8 string */
  if (value) {
    GST_WRITE_UINT16_BE (data, language_code (GST_QT_MUX_DEFAULT_TAG_LANGUAGE));
    /* include 0 terminator */
    memcpy (data + 2, value, len + 1);
  }
  /* 16-bit unsigned int if standalone, otherwise 8-bit */
  if (ivalue >= 0) {
    if (size == 2)
      GST_WRITE_UINT16_BE (data + size - 2, ivalue);
    else {
      GST_WRITE_UINT8 (data + size - 2, ivalue & 0xFF);
      size--;
    }
  }

  atom_moov_add_3gp_tag (moov, fourcc, data, size);
  g_free (data);
}

void
atom_moov_add_3gp_str_tag (AtomMOOV * moov, guint32 fourcc, const gchar * value)
{
  atom_moov_add_3gp_str_int_tag (moov, fourcc, value, -1);
}

void
atom_moov_add_3gp_uint_tag (AtomMOOV * moov, guint32 fourcc, guint16 value)
{
  atom_moov_add_3gp_str_int_tag (moov, fourcc, NULL, value);
}

/*
 * Functions for specifying media types
 */

static void
atom_minf_set_audio (AtomMINF * minf)
{
  atom_minf_clear_handlers (minf);
  minf->smhd = atom_smhd_new ();
}

static void
atom_minf_set_video (AtomMINF * minf, AtomsContext * context)
{
  atom_minf_clear_handlers (minf);
  minf->vmhd = atom_vmhd_new (context);
}

static void
atom_hdlr_set_type (AtomHDLR * hdlr, AtomsContext * context, guint32 comp_type,
    guint32 hdlr_type)
{
  if (context->flavor == ATOMS_TREE_FLAVOR_MOV) {
    hdlr->component_type = comp_type;
  }
  hdlr->handler_type = hdlr_type;
}

static void
atom_mdia_set_hdlr_type_audio (AtomMDIA * mdia, AtomsContext * context)
{
  atom_hdlr_set_type (&mdia->hdlr, context, FOURCC_mhlr, FOURCC_soun);
}

static void
atom_mdia_set_hdlr_type_video (AtomMDIA * mdia, AtomsContext * context)
{
  atom_hdlr_set_type (&mdia->hdlr, context, FOURCC_mhlr, FOURCC_vide);
}

static void
atom_mdia_set_audio (AtomMDIA * mdia, AtomsContext * context)
{
  atom_mdia_set_hdlr_type_audio (mdia, context);
  atom_minf_set_audio (&mdia->minf);
}

static void
atom_mdia_set_video (AtomMDIA * mdia, AtomsContext * context)
{
  atom_mdia_set_hdlr_type_video (mdia, context);
  atom_minf_set_video (&mdia->minf, context);
}

static void
atom_tkhd_set_audio (AtomTKHD * tkhd)
{
  tkhd->volume = 0x0100;
  tkhd->width = tkhd->height = 0;
}

static void
atom_tkhd_set_video (AtomTKHD * tkhd, AtomsContext * context, guint32 width,
    guint32 height)
{
  tkhd->volume = 0;

  /* qt and ISO base media do not contradict, and examples agree */
  tkhd->width = width;
  tkhd->height = height;
}

static void
atom_edts_add_entry (AtomEDTS * edts, EditListEntry * entry)
{
  edts->elst.entries = g_slist_append (edts->elst.entries, entry);
}

/* 
 * Adds a new entry to this trak edits list
 * duration is in the moov's timescale
 * media_time is the offset in the media time to start from (media's timescale)
 * rate is a 32 bits fixed-point
 */
void
atom_trak_add_elst_entry (AtomTRAK * trak, guint32 duration, guint32 media_time,
    guint32 rate)
{
  EditListEntry *entry = g_new (EditListEntry, 1);

  entry->duration = duration;
  entry->media_time = media_time;
  entry->media_rate = rate;

  if (trak->edts == NULL) {
    trak->edts = atom_edts_new ();
  }
  atom_edts_add_entry (trak->edts, entry);
}

/* re-negotiation is prevented at top-level, so only 1 entry expected.
 * Quite some more care here and elsewhere may be needed to
 * support several entries */
static SampleTableEntryMP4A *
atom_trak_add_audio_entry (AtomTRAK * trak, AtomsContext * context,
    guint32 type)
{
  AtomSTSD *stsd = &trak->mdia.minf.stbl.stsd;
  SampleTableEntryMP4A *mp4a = sample_entry_mp4a_new ();

  mp4a->se.header.type = type;
  mp4a->se.kind = AUDIO;
  mp4a->compression_id = -1;
  mp4a->se.data_reference_index = 1;

  stsd->entries = g_list_prepend (stsd->entries, mp4a);
  stsd->n_entries++;
  return mp4a;
}

static SampleTableEntryMP4V *
atom_trak_add_video_entry (AtomTRAK * trak, AtomsContext * context,
    guint32 type)
{
  SampleTableEntryMP4V *mp4v = sample_entry_mp4v_new (context);
  AtomSTSD *stsd = &trak->mdia.minf.stbl.stsd;

  mp4v->se.header.type = type;
  mp4v->se.kind = VIDEO;
  mp4v->se.data_reference_index = 1;
  mp4v->horizontal_resolution = 72 << 16;
  mp4v->vertical_resolution = 72 << 16;
  if (context->flavor == ATOMS_TREE_FLAVOR_MOV) {
    mp4v->spatial_quality = 512;
    mp4v->temporal_quality = 512;
  }

  stsd->entries = g_list_prepend (stsd->entries, mp4v);
  stsd->n_entries++;
  return mp4v;
}

static void
atom_trak_set_constant_size_samples (AtomTRAK * trak, guint32 sample_size)
{
  trak->mdia.minf.stbl.stsz.sample_size = sample_size;
}

static void
atom_trak_set_audio (AtomTRAK * trak, AtomsContext * context)
{
  atom_tkhd_set_audio (&trak->tkhd);
  atom_mdia_set_audio (&trak->mdia, context);
}

static void
atom_trak_set_video (AtomTRAK * trak, AtomsContext * context, guint32 width,
    guint32 height)
{
  atom_tkhd_set_video (&trak->tkhd, context, width, height);
  atom_mdia_set_video (&trak->mdia, context);
}

static void
atom_trak_set_audio_commons (AtomTRAK * trak, AtomsContext * context,
    guint32 rate)
{
  atom_trak_set_audio (trak, context);
  trak->mdia.mdhd.time_info.timescale = rate;
}

static void
atom_trak_set_video_commons (AtomTRAK * trak, AtomsContext * context,
    guint32 rate, guint32 width, guint32 height)
{
  atom_trak_set_video (trak, context, width, height);
  trak->mdia.mdhd.time_info.timescale = rate;
  trak->tkhd.width = width << 16;
  trak->tkhd.height = height << 16;
}

void
atom_trak_set_audio_type (AtomTRAK * trak, AtomsContext * context,
    AudioSampleEntry * entry, guint32 scale, AtomInfo * ext, gint sample_size)
{
  SampleTableEntryMP4A *ste;

  atom_trak_set_audio_commons (trak, context, scale);
  ste = atom_trak_add_audio_entry (trak, context, entry->fourcc);

  trak->is_video = FALSE;
  trak->is_h264 = FALSE;

  ste->version = entry->version;
  ste->compression_id = entry->compression_id;
  ste->sample_size = entry->sample_size;
  ste->sample_rate = entry->sample_rate << 16;
  ste->channels = entry->channels;

  ste->samples_per_packet = entry->samples_per_packet;
  ste->bytes_per_sample = entry->bytes_per_sample;
  ste->bytes_per_packet = entry->bytes_per_packet;
  ste->bytes_per_frame = entry->bytes_per_frame;

  if (ext)
    ste->extension_atoms = g_list_prepend (ste->extension_atoms, ext);

  /* 0 size means variable size */
  atom_trak_set_constant_size_samples (trak, sample_size);
}

AtomInfo *
build_pasp_extension (AtomTRAK * trak, gint par_width, gint par_height)
{
  AtomData *atom_data;
  GstBuffer *buf;
  guint8 *data;

  buf = gst_buffer_new_and_alloc (8);
  data = GST_BUFFER_DATA (buf);

  /* ihdr = image header box */
  GST_WRITE_UINT32_BE (data, par_width);
  GST_WRITE_UINT32_BE (data + 4, par_height);

  atom_data = atom_data_new_from_gst_buffer (FOURCC_pasp, buf);
  gst_buffer_unref (buf);

  return build_atom_info_wrapper ((Atom *) atom_data, atom_data_copy_data,
      atom_data_free);
}

void
atom_trak_set_video_type (AtomTRAK * trak, AtomsContext * context,
    VisualSampleEntry * entry, guint32 scale, GList * ext_atoms_list)
{
  SampleTableEntryMP4V *ste;
  gint dwidth, dheight;
  gint par_n = 0, par_d = 0;

  if ((entry->par_n != 1 || entry->par_d != 1) &&
      (entry->par_n != entry->par_d)) {
    par_n = entry->par_n;
    par_d = entry->par_d;
  }

  dwidth = entry->width;
  dheight = entry->height;
  /* ISO file spec says track header w/h indicates track's visual presentation
   * (so this together with pixels w/h implicitly defines PAR) */
  if (par_n && (context->flavor != ATOMS_TREE_FLAVOR_MOV)) {
    if (par_n > par_d) {
      dwidth = entry->width * par_n / par_d;
      dheight = entry->height;
    } else {
      dwidth = entry->width * par_n / par_d;
      dheight = entry->height;
    }
  }

  atom_trak_set_video_commons (trak, context, scale, dwidth, dheight);
  ste = atom_trak_add_video_entry (trak, context, entry->fourcc);

  trak->is_video = TRUE;
  trak->is_h264 = (entry->fourcc == FOURCC_avc1);

  ste->version = entry->version;
  ste->width = entry->width;
  ste->height = entry->height;
  ste->depth = entry->depth;
  ste->color_table_id = entry->color_table_id;
  ste->frame_count = entry->frame_count;

  if (ext_atoms_list)
    ste->extension_atoms = g_list_concat (ste->extension_atoms, ext_atoms_list);

  /* QT spec has a pasp extension atom in stsd that can hold PAR */
  if (par_n && (context->flavor == ATOMS_TREE_FLAVOR_MOV)) {
    ste->extension_atoms = g_list_append (ste->extension_atoms,
        build_pasp_extension (trak, par_n, par_d));
  }
}

/* some sample description construction helpers */

AtomInfo *
build_esds_extension (AtomTRAK * trak, guint8 object_type, guint8 stream_type,
    const GstBuffer * codec_data)
{
  guint32 track_id;
  AtomESDS *esds;

  track_id = trak->tkhd.track_ID;

  esds = atom_esds_new ();
  esds->es.id = track_id & 0xFFFF;
  esds->es.dec_conf_desc.object_type = object_type;
  esds->es.dec_conf_desc.stream_type = stream_type << 2 | 0x01;

  /* optional DecoderSpecificInfo */
  if (codec_data) {
    DecoderSpecificInfoDescriptor *desc;

    esds->es.dec_conf_desc.dec_specific_info = desc =
        desc_dec_specific_info_new ();
    desc_dec_specific_info_alloc_data (desc, GST_BUFFER_SIZE (codec_data));

    memcpy (desc->data, GST_BUFFER_DATA (codec_data),
        GST_BUFFER_SIZE (codec_data));
  }

  return build_atom_info_wrapper ((Atom *) esds, atom_esds_copy_data,
      atom_esds_free);
}

AtomInfo *
build_mov_aac_extension (AtomTRAK * trak, const GstBuffer * codec_data)
{
  AtomWAVE *wave;
  AtomFRMA *frma;
  Atom *ext_atom;
  GstBuffer *buf;

  /* Add WAVE atom to the MP4A sample table entry */
  wave = atom_wave_new ();

  /* Prepend Terminator atom to the WAVE list first, so it ends up last */
  ext_atom = (Atom *) atom_data_new (FOURCC_null);
  wave->extension_atoms =
      atom_info_list_prepend_atom (wave->extension_atoms, (Atom *) ext_atom,
      (AtomCopyDataFunc) atom_data_copy_data, (AtomFreeFunc) atom_data_free);

  /* Add ESDS atom to WAVE */
  wave->extension_atoms = g_list_prepend (wave->extension_atoms,
      build_esds_extension (trak, ESDS_OBJECT_TYPE_MPEG4_P3,
          ESDS_STREAM_TYPE_AUDIO, codec_data));

  /* Add MP4A atom to the WAVE:
   * not really in spec, but makes offset based players happy */
  buf = gst_buffer_new_and_alloc (4);
  *((guint32 *) GST_BUFFER_DATA (buf)) = 0;
  ext_atom = (Atom *) atom_data_new_from_gst_buffer (FOURCC_mp4a, buf);
  gst_buffer_unref (buf);

  wave->extension_atoms =
      atom_info_list_prepend_atom (wave->extension_atoms, (Atom *) ext_atom,
      (AtomCopyDataFunc) atom_data_copy_data, (AtomFreeFunc) atom_data_free);

  /* Add FRMA to the WAVE */
  frma = atom_frma_new ();
  frma->media_type = FOURCC_mp4a;

  wave->extension_atoms =
      atom_info_list_prepend_atom (wave->extension_atoms, (Atom *) frma,
      (AtomCopyDataFunc) atom_frma_copy_data, (AtomFreeFunc) atom_frma_free);

  return build_atom_info_wrapper ((Atom *) wave, atom_wave_copy_data,
      atom_wave_free);
}

AtomInfo *
build_fiel_extension (gint fields)
{
  AtomData *atom_data;
  GstBuffer *buf;

  if (fields == 1) {
    return NULL;
  }

  buf = gst_buffer_new_and_alloc (1);
  GST_BUFFER_DATA (buf)[0] = (guint8) fields;

  atom_data =
      atom_data_new_from_gst_buffer (GST_MAKE_FOURCC ('f', 'i', 'e', 'l'), buf);
  gst_buffer_unref (buf);

  return build_atom_info_wrapper ((Atom *) atom_data, atom_data_copy_data,
      atom_data_free);
}

AtomInfo *
build_jp2x_extension (const GstBuffer * prefix)
{
  AtomData *atom_data;

  if (!prefix) {
    return NULL;
  }

  atom_data =
      atom_data_new_from_gst_buffer (GST_MAKE_FOURCC ('j', 'p', '2', 'x'),
      prefix);

  return build_atom_info_wrapper ((Atom *) atom_data, atom_data_copy_data,
      atom_data_free);
}

AtomInfo *
build_jp2h_extension (AtomTRAK * trak, gint width, gint height, guint32 fourcc,
    gint ncomp, const GValue * cmap_array, const GValue * cdef_array)
{
  AtomData *atom_data;
  GstBuffer *buf;
  guint8 cenum;
  gint i;
  gint idhr_size = 22;
  gint colr_size = 15;
  gint cmap_size = 0, cdef_size = 0;
  gint cmap_array_size = 0;
  gint cdef_array_size = 0;
  GstByteWriter writer;

  g_return_val_if_fail (cmap_array == NULL ||
      GST_VALUE_HOLDS_ARRAY (cmap_array), NULL);
  g_return_val_if_fail (cdef_array == NULL ||
      GST_VALUE_HOLDS_ARRAY (cdef_array), NULL);

  if (fourcc == GST_MAKE_FOURCC ('s', 'R', 'G', 'B')) {
    cenum = 0x10;
    if (ncomp == 0)
      ncomp = 3;
  } else if (fourcc == GST_MAKE_FOURCC ('G', 'R', 'A', 'Y')) {
    cenum = 0x11;
    if (ncomp == 0)
      ncomp = 1;
  } else if (fourcc == GST_MAKE_FOURCC ('s', 'Y', 'U', 'V')) {
    cenum = 0x12;
    if (ncomp == 0)
      ncomp = 3;
  } else
    return NULL;

  if (cmap_array) {
    cmap_array_size = gst_value_array_get_size (cmap_array);
    cmap_size = 8 + cmap_array_size * 4;
  }
  if (cdef_array) {
    cdef_array_size = gst_value_array_get_size (cdef_array);
    cdef_size = 8 + 2 + cdef_array_size * 6;
  }

  buf = gst_buffer_new_and_alloc (idhr_size + colr_size + cmap_size +
      cdef_size);
  gst_byte_writer_init_with_buffer (&writer, buf, FALSE);

  /* ihdr = image header box */
  gst_byte_writer_put_uint32_be (&writer, 22);
  gst_byte_writer_put_uint32_le (&writer, GST_MAKE_FOURCC ('i', 'h', 'd', 'r'));
  gst_byte_writer_put_uint32_be (&writer, height);
  gst_byte_writer_put_uint32_be (&writer, width);
  gst_byte_writer_put_uint16_be (&writer, ncomp);
  /* 8 bits per component, unsigned */
  gst_byte_writer_put_uint8 (&writer, 0x7);
  /* compression type; reserved */
  gst_byte_writer_put_uint8 (&writer, 0x7);
  /* colour space (un)known */
  gst_byte_writer_put_uint8 (&writer, 0x0);
  /* intellectual property right (box present) */
  gst_byte_writer_put_uint8 (&writer, 0x0);

  /* colour specification box */
  gst_byte_writer_put_uint32_be (&writer, 15);
  gst_byte_writer_put_uint32_le (&writer, GST_MAKE_FOURCC ('c', 'o', 'l', 'r'));

  /* specification method: enumerated */
  gst_byte_writer_put_uint8 (&writer, 0x1);
  /* precedence; reserved */
  gst_byte_writer_put_uint8 (&writer, 0x0);
  /* approximation; reserved */
  gst_byte_writer_put_uint8 (&writer, 0x0);
  /* enumerated colourspace */
  gst_byte_writer_put_uint32_be (&writer, cenum);

  if (cmap_array) {
    gst_byte_writer_put_uint32_be (&writer, cmap_size);
    gst_byte_writer_put_uint32_le (&writer,
        GST_MAKE_FOURCC ('c', 'm', 'a', 'p'));
    for (i = 0; i < cmap_array_size; i++) {
      const GValue *item;
      gint value;
      guint16 cmp;
      guint8 mtyp;
      guint8 pcol;
      item = gst_value_array_get_value (cmap_array, i);
      value = g_value_get_int (item);

      /* value is '(mtyp << 24) | (pcol << 16) | cmp' */
      cmp = value & 0xFFFF;
      mtyp = value >> 24;
      pcol = (value >> 16) & 0xFF;

      if (mtyp == 1)
        GST_WARNING ("MTYP of cmap atom signals Pallete Mapping, but we don't "
            "handle Pallete mapping atoms yet");

      gst_byte_writer_put_uint16_be (&writer, cmp);
      gst_byte_writer_put_uint8 (&writer, mtyp);
      gst_byte_writer_put_uint8 (&writer, pcol);
    }
  }

  if (cdef_array) {
    gst_byte_writer_put_uint32_be (&writer, cdef_size);
    gst_byte_writer_put_uint32_le (&writer,
        GST_MAKE_FOURCC ('c', 'd', 'e', 'f'));
    gst_byte_writer_put_uint16_be (&writer, cdef_array_size);
    for (i = 0; i < cdef_array_size; i++) {
      const GValue *item;
      gint value;
      item = gst_value_array_get_value (cdef_array, i);
      value = g_value_get_int (item);

      gst_byte_writer_put_uint16_be (&writer, i);
      if (value > 0) {
        gst_byte_writer_put_uint16_be (&writer, 0);
        gst_byte_writer_put_uint16_be (&writer, value);
      } else if (value < 0) {
        gst_byte_writer_put_uint16_be (&writer, -value);
        gst_byte_writer_put_uint16_be (&writer, 0);     /* TODO what here? */
      } else {
        gst_byte_writer_put_uint16_be (&writer, 1);
        gst_byte_writer_put_uint16_be (&writer, 0);
      }
    }
  }

  g_assert (gst_byte_writer_get_remaining (&writer) == 0);

  atom_data = atom_data_new_from_gst_buffer (FOURCC_jp2h, buf);
  gst_buffer_unref (buf);

  return build_atom_info_wrapper ((Atom *) atom_data, atom_data_copy_data,
      atom_data_free);
}

AtomInfo *
build_codec_data_extension (guint32 fourcc, const GstBuffer * codec_data)
{
  AtomData *data;
  AtomInfo *result = NULL;

  if (codec_data) {
    data = atom_data_new_from_gst_buffer (fourcc, codec_data);
    result = build_atom_info_wrapper ((Atom *) data, atom_data_copy_data,
        atom_data_free);
  }

  return result;
}

AtomInfo *
build_amr_extension ()
{
  guint8 ext[9];
  GstBuffer *buf;
  AtomInfo *res;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = ext;
  GST_BUFFER_SIZE (buf) = sizeof (ext);

  /* vendor */
  GST_WRITE_UINT32_LE (ext, 0);
  /* decoder version */
  GST_WRITE_UINT8 (ext + 4, 0);
  /* mode set (all modes) */
  GST_WRITE_UINT16_BE (ext + 5, 0x81FF);
  /* mode change period (no restriction) */
  GST_WRITE_UINT8 (ext + 7, 0);
  /* frames per sample */
  GST_WRITE_UINT8 (ext + 8, 1);

  res = build_codec_data_extension (GST_MAKE_FOURCC ('d', 'a', 'm', 'r'), buf);
  gst_buffer_unref (buf);
  return res;
}

AtomInfo *
build_h263_extension ()
{
  guint8 ext[7];
  GstBuffer *buf;
  AtomInfo *res;

  buf = gst_buffer_new ();
  GST_BUFFER_DATA (buf) = ext;
  GST_BUFFER_SIZE (buf) = sizeof (ext);

  /* vendor */
  GST_WRITE_UINT32_LE (ext, 0);
  /* decoder version */
  GST_WRITE_UINT8 (ext + 4, 0);
  /* level / profile */
  /* FIXME ? maybe ? obtain somewhere; baseline for now */
  GST_WRITE_UINT8 (ext + 5, 10);
  GST_WRITE_UINT8 (ext + 6, 0);

  res = build_codec_data_extension (GST_MAKE_FOURCC ('d', '2', '6', '3'), buf);
  gst_buffer_unref (buf);
  return res;
}

AtomInfo *
build_gama_atom (gdouble gamma)
{
  AtomInfo *res;
  guint32 gamma_fp;
  GstBuffer *buf;

  /* convert to uint32 from fixed point */
  gamma_fp = (guint32) 65536 *gamma;

  buf = gst_buffer_new_and_alloc (4);
  GST_WRITE_UINT32_BE (GST_BUFFER_DATA (buf), gamma_fp);
  res = build_codec_data_extension (FOURCC_gama, buf);
  gst_buffer_unref (buf);
  return res;
}

AtomInfo *
build_SMI_atom (const GstBuffer * seqh)
{
  AtomInfo *res;
  GstBuffer *buf;

  /* the seqh plus its size and fourcc */
  buf = gst_buffer_new_and_alloc (GST_BUFFER_SIZE (seqh) + 8);

  GST_WRITE_UINT32_LE (GST_BUFFER_DATA (buf), FOURCC_SEQH);
  GST_WRITE_UINT32_BE (GST_BUFFER_DATA (buf) + 4, GST_BUFFER_SIZE (seqh));
  memcpy (GST_BUFFER_DATA (buf) + 8, GST_BUFFER_DATA (seqh),
      GST_BUFFER_SIZE (seqh));
  res = build_codec_data_extension (FOURCC_SMI_, buf);
  gst_buffer_unref (buf);
  return res;
}

static AtomInfo *
build_ima_adpcm_atom (gint channels, gint rate, gint blocksize)
{
  AtomData *atom_data;
  GstBuffer *buf;
  guint8 *data;
  const gint ima_adpcm_atom_size = 20;
  guint32 fourcc;
  gint samplesperblock;
  gint bytespersec;

  /* The FOURCC for WAV codecs in QT is 'ms' followed by the 16 bit wave codec
     identifier. Note that the identifier here is big-endian, but when used
     within the WAVE header (below), it's little endian. */
  fourcc = MS_WAVE_FOURCC (0x11);

  buf = gst_buffer_new_and_alloc (ima_adpcm_atom_size);
  data = GST_BUFFER_DATA (buf);

  /* This atom's content is a WAVE header, including 2 bytes of extra data.
     Note that all of this is little-endian, unlike most stuff in qt. */
  /* 4 bytes header per channel (including 1 sample). Then 2 samples per byte
     for the rest. Simplifies to this. */
  samplesperblock = 2 * blocksize / channels - 7;
  bytespersec = rate * blocksize / samplesperblock;
  GST_WRITE_UINT16_LE (data, 0x11);
  GST_WRITE_UINT16_LE (data + 2, channels);
  GST_WRITE_UINT32_LE (data + 4, rate);
  GST_WRITE_UINT32_LE (data + 8, bytespersec);
  GST_WRITE_UINT16_LE (data + 12, blocksize);
  GST_WRITE_UINT16_LE (data + 14, 4);
  GST_WRITE_UINT16_LE (data + 16, 2);   /* Two extra bytes */
  GST_WRITE_UINT16_LE (data + 18, samplesperblock);

  atom_data = atom_data_new_from_gst_buffer (fourcc, buf);
  gst_buffer_unref (buf);

  return build_atom_info_wrapper ((Atom *) atom_data, atom_data_copy_data,
      atom_data_free);
}

AtomInfo *
build_ima_adpcm_extension (gint channels, gint rate, gint blocksize)
{
  AtomWAVE *wave;
  AtomFRMA *frma;
  Atom *ext_atom;

  /* Add WAVE atom */
  wave = atom_wave_new ();

  /* Prepend Terminator atom to the WAVE list first, so it ends up last */
  ext_atom = (Atom *) atom_data_new (FOURCC_null);
  wave->extension_atoms =
      atom_info_list_prepend_atom (wave->extension_atoms, (Atom *) ext_atom,
      (AtomCopyDataFunc) atom_data_copy_data, (AtomFreeFunc) atom_data_free);

  /* Add wave ima adpcm atom to WAVE */
  wave->extension_atoms = g_list_prepend (wave->extension_atoms,
      build_ima_adpcm_atom (channels, rate, blocksize));

  /* Add FRMA to the WAVE */
  frma = atom_frma_new ();
  frma->media_type = MS_WAVE_FOURCC (0x11);

  wave->extension_atoms =
      atom_info_list_prepend_atom (wave->extension_atoms, (Atom *) frma,
      (AtomCopyDataFunc) atom_frma_copy_data, (AtomFreeFunc) atom_frma_free);

  return build_atom_info_wrapper ((Atom *) wave, atom_wave_copy_data,
      atom_wave_free);
}
