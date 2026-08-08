# Phases 8-11 authoring stack

The KairoAssets authoring surface now uses `EditableMesh` as the single topology kernel and layers transactional polygon operations, seam-derived UV islands, PBR material and texture import settings, deterministic project-owned mesh documents, and bounded sculpt production workflows on top of it.

`ModelingOperations` provides edge split, knife, triangulate, dissolve, duplicate, bridge, fill, normal flipping, and a non-destructive modifier stack without bypassing `EditableMesh` validation. `MaterialAuthoring` owns UV island construction/packing, material-slot assignment, PBR channel inspection, and texture color-space/semantic/mipmap authoring settings. `EditableMeshDocument` round-trips topology, UVs, seams, modifiers, material artifacts, and texture reimport settings in the bounded `kairo.editable-mesh.v1` format.

`SculptProduction` adds bounded undo memory/stroke budgets, incremental dirty-vertex updates for viewport uploads, uniform deterministic remeshing within hard topology budgets, and bounded multiresolution levels. The current remesher is intentionally deterministic uniform subdivision; adaptive dynamic topology can replace that strategy later behind the same budget contract.
