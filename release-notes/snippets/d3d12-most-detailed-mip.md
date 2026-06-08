## d3d12: most-detailed-mip property to select the base mip level

New property `most-detailed-mip` on `d3d12converter` (also exposed on
`d3d12convert`, `d3d12scale`, `d3d12videosink` and `d3d12swapchainsink`)
to choose the most detailed (base) mip level the converter starts
generating and sampling from when mipmap generation is enabled. Skipping
the expensive high-resolution levels tunes the quality vs GPU-load
balance and reduces aliasing on large downscales. The `max-mip-levels`
range is counted from this base level. Default 0 preserves the existing
behavior.
