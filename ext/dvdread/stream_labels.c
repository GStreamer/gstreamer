/* GStreamer
 * Copyright (C) <2005> Stephane Loeuillet <stephane.loeuillet@tiscali.fr>
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

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "dvdreadsrc.h"
#include "stream_labels.h"
#include <gst/gst-i18n-plugin.h>

GHashTable *
dvdreadsrc_init_languagelist (void)
{
  GHashTable *languagelist = NULL;

  languagelist = g_hash_table_new (g_str_hash, g_str_equal);

  g_hash_table_insert (languagelist, "aa", _("Afar"));
  g_hash_table_insert (languagelist, "ab", _("Abkhazian"));
  g_hash_table_insert (languagelist, "af", _("Afrikaans"));
  g_hash_table_insert (languagelist, "am", _("Amharic"));
  g_hash_table_insert (languagelist, "ar", _("Arabic"));
  g_hash_table_insert (languagelist, "as", _("Assamese"));
  g_hash_table_insert (languagelist, "ax", _("Aymara"));
  g_hash_table_insert (languagelist, "az", _("Azerbaijani"));
  g_hash_table_insert (languagelist, "ba", _("Bashkir"));
  g_hash_table_insert (languagelist, "be", _("Byelorussian"));
  g_hash_table_insert (languagelist, "bg", _("Bulgarian"));
  g_hash_table_insert (languagelist, "bh", _("Bislama"));
  g_hash_table_insert (languagelist, "bn", _("Bengali"));
  g_hash_table_insert (languagelist, "bo", _("Tibetan"));
  g_hash_table_insert (languagelist, "br", _("Breton"));
  g_hash_table_insert (languagelist, "ca", _("Catalan"));
  g_hash_table_insert (languagelist, "co", _("Corsican"));
  g_hash_table_insert (languagelist, "cs", _("Czech"));
  g_hash_table_insert (languagelist, "cy", _("Welsh"));
  g_hash_table_insert (languagelist, "da", _("Danish"));
  g_hash_table_insert (languagelist, "de", _("German"));
  g_hash_table_insert (languagelist, "dz", _("Bhutani"));
  g_hash_table_insert (languagelist, "el", _("Greek"));
  g_hash_table_insert (languagelist, "en", _("English"));
  g_hash_table_insert (languagelist, "eo", _("Esperanto"));
  g_hash_table_insert (languagelist, "es", _("Spanish"));
  g_hash_table_insert (languagelist, "et", _("Estonian"));
  g_hash_table_insert (languagelist, "eu", _("Basque"));
  g_hash_table_insert (languagelist, "fa", _("Persian"));
  g_hash_table_insert (languagelist, "fi", _("Finnish"));
  g_hash_table_insert (languagelist, "fj", _("Fiji"));
  g_hash_table_insert (languagelist, "fo", _("Faeroese"));
  g_hash_table_insert (languagelist, "fr", _("French"));
  g_hash_table_insert (languagelist, "fy", _("Frisian"));
  g_hash_table_insert (languagelist, "ga", _("Irish"));
  g_hash_table_insert (languagelist, "gd", _("Scots/Gaelic"));
  g_hash_table_insert (languagelist, "gl", _("Galician"));
  g_hash_table_insert (languagelist, "gn", _("Guarani"));
  g_hash_table_insert (languagelist, "gu", _("Gujarati"));
  g_hash_table_insert (languagelist, "ha", _("Hausa"));
  g_hash_table_insert (languagelist, "hi", _("Hindi"));
  g_hash_table_insert (languagelist, "hr", _("Croatian"));
  g_hash_table_insert (languagelist, "hu", _("Hungarian"));
  g_hash_table_insert (languagelist, "hy", _("Armenian"));
  g_hash_table_insert (languagelist, "ia", _("Interlingua"));
  g_hash_table_insert (languagelist, "in", _("Indonesian"));
  g_hash_table_insert (languagelist, "is", _("Icelandic"));
  g_hash_table_insert (languagelist, "it", _("Italian"));
  g_hash_table_insert (languagelist, "iw", _("Hebrew"));
  g_hash_table_insert (languagelist, "ja", _("Japanese"));
  g_hash_table_insert (languagelist, "ji", _("Yiddish"));
  g_hash_table_insert (languagelist, "jw", _("Javanese"));
  g_hash_table_insert (languagelist, "ka", _("Georgian"));
  g_hash_table_insert (languagelist, "kk", _("Kazakh"));
  g_hash_table_insert (languagelist, "kl", _("Greenlandic"));
  g_hash_table_insert (languagelist, "km", _("Cambodian"));
  g_hash_table_insert (languagelist, "kn", _("Kannada"));
  g_hash_table_insert (languagelist, "ko", _("Korean"));
  g_hash_table_insert (languagelist, "ks", _("Kashmiri"));
  g_hash_table_insert (languagelist, "ku", _("Kurdish"));
  g_hash_table_insert (languagelist, "ky", _("Kirghiz"));
  g_hash_table_insert (languagelist, "la", _("Latin"));
  g_hash_table_insert (languagelist, "ln", _("Lingala"));
  g_hash_table_insert (languagelist, "lo", _("Laothian"));
  g_hash_table_insert (languagelist, "lt", _("Lithuanian"));
  g_hash_table_insert (languagelist, "lv", _("Latvian/Lettish"));
  g_hash_table_insert (languagelist, "mg", _("Malagasy"));
  g_hash_table_insert (languagelist, "mi", _("Maori"));
  g_hash_table_insert (languagelist, "mk", _("Macedonian"));
  g_hash_table_insert (languagelist, "ml", _("Malayalam"));
  g_hash_table_insert (languagelist, "mn", _("Mongolian"));
  g_hash_table_insert (languagelist, "mo", _("Moldavian"));
  g_hash_table_insert (languagelist, "mr", _("Marathi"));
  g_hash_table_insert (languagelist, "ms", _("Malay"));
  g_hash_table_insert (languagelist, "mt", _("Maltese"));
  g_hash_table_insert (languagelist, "my", _("Burmese"));
  g_hash_table_insert (languagelist, "na", _("Nauru"));
  g_hash_table_insert (languagelist, "ne", _("Nepali"));
  g_hash_table_insert (languagelist, "nl", _("Dutch"));
  g_hash_table_insert (languagelist, "no", _("Norwegian"));
  g_hash_table_insert (languagelist, "or", _("Oriya"));
  g_hash_table_insert (languagelist, "pa", _("Punjabi"));
  g_hash_table_insert (languagelist, "pl", _("Polish"));
  g_hash_table_insert (languagelist, "ps", _("Pashto/Pushto"));
  g_hash_table_insert (languagelist, "pt", _("Portuguese"));
  g_hash_table_insert (languagelist, "qu", _("Quechua"));
  g_hash_table_insert (languagelist, "rm", _("Rhaeto-Romance"));
  g_hash_table_insert (languagelist, "ro", _("Romanian"));
  g_hash_table_insert (languagelist, "ru", _("Russian"));
  g_hash_table_insert (languagelist, "sa", _("Kinyarwanda"));
  g_hash_table_insert (languagelist, "sd", _("Sanskrit"));
  g_hash_table_insert (languagelist, "sh", _("Serbo-Croatian"));
  g_hash_table_insert (languagelist, "si", _("Singhalese"));
  g_hash_table_insert (languagelist, "sk", _("Slovak"));
  g_hash_table_insert (languagelist, "sl", _("Slovenian"));
  g_hash_table_insert (languagelist, "sm", _("Samoan"));
  g_hash_table_insert (languagelist, "sn", _("Shona"));
  g_hash_table_insert (languagelist, "so", _("Somali"));
  g_hash_table_insert (languagelist, "sq", _("Albanian"));
  g_hash_table_insert (languagelist, "sr", _("Serbian"));
  g_hash_table_insert (languagelist, "su", _("Sundanese"));
  g_hash_table_insert (languagelist, "sv", _("Swedish"));
  g_hash_table_insert (languagelist, "sw", _("Swahili"));
  g_hash_table_insert (languagelist, "ta", _("Tamil"));
  g_hash_table_insert (languagelist, "te", _("Tegulu"));
  g_hash_table_insert (languagelist, "tg", _("Tajik"));
  g_hash_table_insert (languagelist, "th", _("Thai"));
  g_hash_table_insert (languagelist, "ti", _("Tigrinya"));
  g_hash_table_insert (languagelist, "tk", _("Turkmen"));
  g_hash_table_insert (languagelist, "tl", _("Tagalog"));
  g_hash_table_insert (languagelist, "to", _("Tonga"));
  g_hash_table_insert (languagelist, "tr", _("Turkish"));
  g_hash_table_insert (languagelist, "tt", _("Tatar"));
  g_hash_table_insert (languagelist, "tw", _("Twi"));
  g_hash_table_insert (languagelist, "uk", _("Ukrainian"));
  g_hash_table_insert (languagelist, "ur", _("Urdu"));
  g_hash_table_insert (languagelist, "uz", _("Uzbek"));
  g_hash_table_insert (languagelist, "vi", _("Vietnamese"));
  g_hash_table_insert (languagelist, "vo", _("Volapuk"));
  g_hash_table_insert (languagelist, "wo", _("Wolof"));
  g_hash_table_insert (languagelist, "xh", _("Xhosa"));
  g_hash_table_insert (languagelist, "yo", _("Yoruba"));
  g_hash_table_insert (languagelist, "zh", _("Chinese"));
  g_hash_table_insert (languagelist, "zu", _("Zulu"));

  return languagelist;
}

void
dvdreadsrc_get_audio_stream_labels (ifo_handle_t * vts_file,
    GHashTable * languagelist)
{
  GList *audio_stream_label = NULL;

  if (vts_file->vts_pgcit) {
    int i;

    /* 8 audio streams maximum */
    for (i = 0; i < 8; i++) {
      const gchar *format, *channel_nb, *language = NULL;
      guchar language_code[3] = "??";
      gchar *streamlabel;

      if (vts_file->vts_pgcit->pgci_srp[0].pgc->audio_control[i] & 0x8000) {
        audio_attr_t *audio = &vts_file->vtsi_mat->vts_audio_attr[i];

        if (audio->lang_type == 1) {
          language_code[0] = (audio->lang_code >> 8);
          language_code[1] = (audio->lang_code & 0xFF);
          language = g_hash_table_lookup (languagelist, language_code);
        }

        if (!language) {
          language = "?";
        }

        switch (audio->audio_format) {
          case 0:
            format = _("Dolby AC-3");
            break;
          case 2:
          case 3:
            format = _("MPEG layer I, II or III");
            break;
          case 4:
            format = _("LPCM");
            break;
          case 6:
            format = _("Digital Theatre System");
            break;
          default:
            format = "?";
        }

        switch (audio->channels) {
          case 1:
            channel_nb = _("Stereo");
            break;
          case 5:
            channel_nb = _("5.1");
            break;
          default:
            channel_nb = "?";
        }

        streamlabel = g_strdup_printf ("%u : %s, %s %s", i + 1, language,
            format, channel_nb);
        audio_stream_label = g_list_append (audio_stream_label, streamlabel);   /* "French, Dolby AC-3 Stereo" */

        printf ("%u : %s, %s %s\n", i + 1, language, format, channel_nb);
      }
    }
  }

  g_list_foreach (audio_stream_label, (GFunc) g_free, NULL);
  g_list_free (audio_stream_label);
}

void
dvdreadsrc_get_subtitle_stream_labels (ifo_handle_t * vts_file,
    GHashTable * languagelist)
{

}
