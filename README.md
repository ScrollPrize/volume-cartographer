[![VC3D](docs/images/banner.svg)](https://github.com/ScrollPrize/volume-cartographer)

**VC3D** is a toolkit and set of cross-platform C++ libraries for
virtually unwrapping volumetric datasets. It was designed to recover text from
CT scans of ancient, badly damaged manuscripts, but can be applied in many
volumetric analysis applications.

## WIP 3D volume slicing
- ome-zarr enabled VC is available as *VC3D* alongside the existing VC binary
- this branch implements (OME)-Zarr based volume viewing and arbitrary plane slicing, currently used to show XY,YZ,XZ planes + 1 plane with free rotation
- along the way a lot of functionaltiy was disabled/broken - for now use it just to look at stuff, do not touch segmentation
- usage
  - you need a Zarr volume (e.g. https://dl.ash2txt.org/other/dev/scrolls/1/volumes/54keV_7.91um.zarr/) in the volumes directory of your volpkg
  - it needs a meta.json in the .zarr directory, which can be identical to the regular volume apart from:
    - adding "format":"zarr"
    - changing "name" so you can identify it in VC
    - change the "UUID" (e.g. also add -zarr)
  - currently only uint8 datatype is supported (but any compression thats compiled into z5)
  - currently scale 0 (highest resolution) will be ignored and can be left out (which means ~90GB of data for the volume linked above)
    - note that the metadata for scale 0 still needs to be present ("0/.zarray")
  - by default VC will only load scale 1 or higher from the Zarr voluem, so you do not need to download the highest resolution scale 0
  - the default chunk cache is 10GB of RAM
- features
  - big yellow circle marks the slicing center
  - right or middle click + drag to move around
  - ctrl click to set slicing center to the selected location
  - free rotation plane slice:
    - change free plane normal in the bottom left
    - scroll to zoom in/out
  - control point based height map slice with IDW for interpolation
    - click anywhere to add a control point for the 3d slice/segment
  - fourth panel (center-down) contains the free plane slice, rightmost panel the 3d slice
