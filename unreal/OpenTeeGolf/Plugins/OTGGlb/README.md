# OTGGlb

A glTF binary (`.glb`) read at run time. Every primitive's positions, normals, vertex colours, UVs
and triangles are merged into one mesh. The exported courses' water, paths, buildings, cars and far
land are `.glb` files (the web version's `public/js/export-unreal.js`).

## Main pieces

`Glb/OTGGlb.h`:

- `otg::FGlbMesh`: `Positions`, `Normals`, `Colors`, `UVs` and `Indices`, in the file's own frame.
- `otg::ReadGlb(file, out)`: false if the file is missing or not a glb.

## How the game uses it

The runtime course (`World/`) reads each export's `.glb` files and makes meshes of them
(`MeshFromGlb` in `OTGCourseShared.h`).

## Taking it into another project

Copy this folder into your project's `Plugins/` folder (it needs the engine's Json module). It's enabled by default. Add
`"OTGGlb"` to your module's dependencies in its `.Build.cs`.
