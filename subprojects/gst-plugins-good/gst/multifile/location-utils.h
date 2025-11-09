/* GStreamer multifile location template pattern checking utilities
 *
 * Copyright (C) 2025 Tim-Philipp Müller <tim centricular com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 *
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

#include <glib.h>

G_BEGIN_DECLS

gchar *   multifile_utils_printf_string_from_template (gpointer log_object,
                                                       const gchar * tmpl,
                                                       gint n);

gboolean  multifile_utils_check_template_string       (gpointer log_object,
                                                       const gchar * tmpl);

G_END_DECLS
