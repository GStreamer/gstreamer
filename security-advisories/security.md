---
title: "Security Advisory {{ strings.TrimPrefix "sa-" .File.ContentBaseName }} (CVE-{{ substr .File.ContentBaseName 3 4 }}-XXXX)"
id: "GStreamer-SA-{{ strings.TrimPrefix "sa-" .File.ContentBaseName }}"
date: {{ now.Format "2006-01-02T15:04:05" }}
summary: "Example summary"
modules: ["gst-plugins-XYZ"]
affected_versions: "GStreamer gst-plugins-XYZ < 1.XX.X"
fixed_versions: ["1.XX.X"]
# ids: first entry is the advisory id, then ZDI/GHSL/CVE ids, shown as listed here
ids: ["GStreamer-SA-{{ strings.TrimPrefix "sa-" .File.ContentBaseName }}", "CVE-{{ substr .File.ContentBaseName 3 4 }}-XXXX"]
cve: ["CVE-{{ substr .File.ContentBaseName 3 4 }}-XXXX"]
patches: ["https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/XXXX.patch"]
# The summary table at the top of the page is generated from this frontmatter
# (summary, date, affected_versions, ids). Do not add it to the body.
# Date: YYYY-MM-DDTHH:MM:SS shows the time; a plain date shows the day only.
# Optional: list_summary (text for the /security/ table when it must differ).
# summary should be one line. affected_versions: which module and versions.
# Body sections: Details (type of vulnerability), Impact (leak, crash, code
# execution...), Threat mitigation (factors reducing exposure), Workarounds
# (config changes limiting exposure), Solution (releases fixing it), References.
# published | cve-pending
status: published
---

## Details

## Impact

## Threat mitigation

## Workarounds

## Solution

## References

### The GStreamer project

- [https://gstreamer.freedesktop.org](https://gstreamer.freedesktop.org)

### CVE Database Entries

- [CVE-{{ substr .File.ContentBaseName 3 4 }}-XXXX](https://cve.mitre.org/cgi-bin/cvename.cgi?name=CVE-{{ substr .File.ContentBaseName 3 4 }}-XXXX)

### GStreamer 1.XX.X release

- [Release Notes](/releases/1.XX/#1.XX.X)
- [GStreamer Plugins XYZ 1.XX.X](/src/gst-plugins-XYZ/gst-plugins-XYZ-1.XX.X.tar.xz)

### Patches

- [Patch](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/XXXX.patch)
