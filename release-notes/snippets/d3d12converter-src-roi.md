## d3d12converter: src-roi properties for mipmap level selection

New properties `src-roi-width` and `src-roi-height` on `d3d12converter`
(also proxied through `d3d12swapchainsink`) to tell the converter the
actual source ROI, so the correct target mipmap level can be selected
when only a sub-region of the source is sampled (e.g. via uv-remap of
d3d12swapchainsink). Default 0 preserves existing behavior (treat the
entire input as the ROI).
