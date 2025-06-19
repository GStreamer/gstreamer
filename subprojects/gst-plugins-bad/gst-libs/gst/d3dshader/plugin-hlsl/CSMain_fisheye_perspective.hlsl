/* GStreamer
 * Copyright (C) 2025 Seungha Yang <seungha@centricular.com>
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
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#ifdef BUILDING_HLSL
RWTexture2D<float4> uvLUT : register(u0);

cbuffer Parameters : register(b0)
{
  float2 fisheyeCenter;
  float2 fisheyeRadius;

  float maxAngle;
  float horizontalFOV; // unused
  float verticalFOV; // unused
  float rollAngle; // Unused

  float2 roiOffset;
  float2 roiScale;

  float padding;
  float invFocalLenX;
  float invFocalLenY;
  float otherPadding;

  float3x3 RotationMatrix;
};

[numthreads(8, 8, 1)]
void ENTRY_POINT (uint3 DTid : SV_DispatchThreadID)
{
  uint width, height;
  uvLUT.GetDimensions(width, height);
  if (DTid.x >= width || DTid.y >= height)
    return;

  // Compute normalized screen coordinate
  float2 uv = float2(DTid.xy) / float2(width, height);

  // Apply ROI cropping and scaling
  float2 uv_roi = roiOffset + uv * roiScale;

  // Convert to NDC [-1, 1]
  float2 uv_ndc = uv_roi * 2.0 - 1.0;

  // Compute view ray from perspective FOV (pinhole model)
  float x = invFocalLenX * uv_ndc.x;
  float y = invFocalLenY * uv_ndc.y;
  float3 localRay = normalize(float3(x, y, 1.0));

  float3 worldRay = normalize(mul(RotationMatrix, localRay));

  // Compute angle from Z-axis (zenith angle)
  float angle = acos(worldRay.z);

  float4 fishUV = float4(0.0, 0.0, 0.0, 1.0);
  if (angle <= maxAngle) {
    // Project to fisheye image using equidistant projection
    float phi = atan2(worldRay.y, worldRay.x);

    float2 r = (fisheyeRadius / maxAngle) * angle;
    fishUV.xy = fisheyeCenter + r * float2(cos(phi), sin(phi));
  } else {
    // Out of view
    fishUV.w = 0.0;
  }

  uvLUT[DTid.xy] = fishUV;
}
#else
static const char str_CSMain_fisheye_perspective[] =
"RWTexture2D<float4> uvLUT : register(u0);\n"
"\n"
"cbuffer Parameters : register(b0)\n"
"{\n"
"  float2 fisheyeCenter;\n"
"  float2 fisheyeRadius;\n"
"\n"
"  float maxAngle;\n"
"  float horizontalFOV; // unused\n"
"  float verticalFOV; // unused\n"
"  float rollAngle; // Unused\n"
"\n"
"  float2 roiOffset;\n"
"  float2 roiScale;\n"
"\n"
"  float padding;\n"
"  float invFocalLenX;\n"
"  float invFocalLenY;\n"
"  float otherPadding;\n"
"\n"
"  float3x3 RotationMatrix;\n"
"};\n"
"\n"
"[numthreads(8, 8, 1)]\n"
"void ENTRY_POINT (uint3 DTid : SV_DispatchThreadID)\n"
"{\n"
"  uint width, height;\n"
"  uvLUT.GetDimensions(width, height);\n"
"  if (DTid.x >= width || DTid.y >= height)\n"
"    return;\n"
"\n"
"  // Compute normalized screen coordinate\n"
"  float2 uv = float2(DTid.xy) / float2(width, height);\n"
"\n"
"  // Apply ROI cropping and scaling\n"
"  float2 uv_roi = roiOffset + uv * roiScale;\n"
"\n"
"  // Convert to NDC [-1, 1]\n"
"  float2 uv_ndc = uv_roi * 2.0 - 1.0;\n"
"\n"
"  // Compute view ray from perspective FOV (pinhole model)\n"
"  float x = invFocalLenX * uv_ndc.x;\n"
"  float y = invFocalLenY * uv_ndc.y;\n"
"  float3 localRay = normalize(float3(x, y, 1.0));\n"
"\n"
"  float3 worldRay = normalize(mul(RotationMatrix, localRay));\n"
"\n"
"  // Compute angle from Z-axis (zenith angle)\n"
"  float angle = acos(worldRay.z);\n"
"\n"
"  float4 fishUV = float4(0.0, 0.0, 0.0, 1.0);\n"
"  if (angle <= maxAngle) {\n"
"    // Project to fisheye image using equidistant projection\n"
"    float phi = atan2(worldRay.y, worldRay.x);\n"
"\n"
"    float2 r = (fisheyeRadius / maxAngle) * angle;\n"
"    fishUV.xy = fisheyeCenter + r * float2(cos(phi), sin(phi));\n"
"  } else {\n"
"    // Out of view\n"
"    fishUV.w = 0.0;\n"
"  }\n"
"\n"
"  uvLUT[DTid.xy] = fishUV;\n"
"}\n";
#endif
