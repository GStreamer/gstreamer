/**
 * SECTION:gestimeoverlayclip
 * @title: GESTimeOverlayClip
 * @short_description: Source with a time overlay on top
 * @symbols:
 *   - ges_source_clip_new_time_overlay
 *
 * A #GESSourceClip that overlays timing information on top.
 *
 * ## Asset
 *
 * The default asset ID is "time-overlay" (of type #GES_TYPE_SOURCE_CLIP),
 * but the framerate and video size can be overridden using an ID of the form:
 *
 * ```
 * time-overlay, framerate=60/1, width=1920, height=1080, max-duration=5.0
 * ```
 *
 * ## Children properties
 *
 * {{ libs/GESTimeOverlayClip-children-props.md }}
 *
 * ## Symbols
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "ges-asset.h"
#include "ges-time-overlay-clip.h"


/**
 * ges_source_clip_new_time_overlay:
 *
 * Creates a new #GESSourceClip that renders a time overlay on top
 *
 * Returns: (transfer floating) (nullable): The newly created #GESSourceClip,
 * or %NULL if there was an error.
 * Since: 1.18
 */
GESSourceClip *
ges_source_clip_new_time_overlay (void)
{
  GESSourceClip *new_clip;
  GESAsset *asset = ges_asset_request (GES_TYPE_SOURCE_CLIP,
      "time-overlay", NULL);

  new_clip = GES_SOURCE_CLIP (ges_asset_extract (asset, NULL));
  gst_object_unref (asset);

  return new_clip;
}
