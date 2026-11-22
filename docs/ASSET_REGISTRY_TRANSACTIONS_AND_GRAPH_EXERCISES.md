# AssetRegistry under a microscope: identity, path indexes and safe dependencies

Read [AssetRegistry.cppm](../AssetRegistry.cppm) together with [AssetManifest.cppm](../AssetManifest.cppm), [DerivedDataCache.cppm](../DerivedDataCache.cppm) and [the full asset pipeline walkthrough](ASSET_PIPELINE_WALKTHROUGH.md). This exercise teaches why a source-file path, persistent AssetID, import artifact and native GPU handle must remain separate.

## 1. The two-index invariant

AssetRegistry owns:

- an **AssetID → AssetMetadata** table that contains the complete authored metadata;
- a **normalized, case-folded portable path → AssetID** table used to find source files.

A metadata record includes its type, origin, path, revision, importer and typed dependencies. Every registered path must map to exactly the same ID as its metadata record. The registry may not contain an extra path key pointing to no asset, and a live metadata record should not lack its corresponding portable path.

The path key is case-folded because content may be developed on one filesystem and shipped on another. Two files that differ only in capitalization can collide on a case-insensitive destination. The portable asset path policy prevents registering both as separate stable identities.

**Exercise:** register a texture at Art/Wood.PNG, then try to register another at art/wood.png. Explain why a project manifest should refuse the collision even if your current filesystem technically permits both names.

## 2. Why Create and Insert have different responsibilities

**Create(AssetRegistration)** generates a new persistent AssetID, retrying collisions up to a bounded budget, then validates the metadata and references. It locks the registry for the critical update and inserts the path index before the metadata record. If metadata-map insertion throws, it removes the new path entry and rethrows, preserving the two-map invariant.

**Insert(AssetMetadata)** instead preserves an existing ID from a saved manifest or import. If loading generated new IDs on every run, authored scenes/materials could no longer resolve their references. Insert performs the same uniqueness/dependency checks and path rollback.

The important guarantee is not “nothing ever throws.” It is that a failed operation does not leave a half-registered asset visible to other readers.

## 3. Typed references and why they matter

Suppose the registry contains these records:

| ID | Type | Path | Dependencies |
| --- | --- | --- | --- |
| T | Texture | textures/wood.png | none |
| M | Material | materials/wood.mat | Texture T |
| S | Scene | scenes/room.scene | Material M |

Here S → M → T is a directed dependency graph. The edge stores **both target ID and expected type**. A Material reference to an existing Audio ID is invalid even when the UUID is correctly formed. **Resolve<T>** checks the compile-time expected type against the record's runtime metadata; type safety is not optional simply because the source file exists.

**SetDependencies** creates a temporary proposed metadata record, checks target existence and types, uses reachability to reject cycles, then publishes the list and increments revision under an exclusive lock.

**Exercise:** attempt to add T → S. The prior graph S → M → T already reaches T from S, so the new edge creates T → S → M → T and must be rejected. Verify no partial dependency list or revision increment occurred.

## 4. How cycle detection actually works

For each proposed dependency D of asset A, **ValidateDependencies** checks whether following edges from D can reach A. **Reaches** uses depth-first recursion and a visited-ID set to avoid traversing the same node forever or repeatedly exploring shared descendants.

If D can reach A, then adding A → D would create a cycle. The algorithm is straightforward but validation cost depends on how many vertices/edges each dependency can reach. Revalidating many candidate records independently may visit parts of a large graph multiple times.

**Exercise:** build a diamond A → B,C; B,C → D, then attempt D → A. Explain why the visited set avoids duplicate traversal, and why a single source-independent schema validator may still need a scalability benchmark for very large manifests.

## 5. Move preserves ID and increments revision

Moving T from textures/wood.png to textures/wood-v2.png must not change T's identity. Scenes and materials continue storing AssetID T; only its portable path mapping and revision change.

**Move** first validates the new path and checks collisions. It handles a change in capitalization with the same folded key separately. For a different key, it checks revision overflow, inserts the new path mapping before removing the old one, swaps the validated filesystem path, and increments revision. This insertion-first order is an exception-safety decision: if the new map allocation fails, the old index is still valid.

The metadata revision acts as a monotonic change signal to provenance and import systems. A wraparound of the revision counter would be dangerous and is explicitly rejected.

**Exercise:** move an asset twice, including a case-only rename. Check that ID equality holds, path lookup changes as expected, revision advances only when metadata actually changes, and an attempted collision leaves the prior path intact.

## 6. Why Remove refuses to cascade

**Remove(T)** inspects the other records' dependencies. If M still references T, removing T would create a dangling authored material reference. The registry throws instead of silently editing M or deleting S.

This makes deleting a texture an explicit authoring transaction: first rebind or remove dependents, then delete the texture. The same safety rule applies whether the Editor performs the change or a command-line import tool requests it.

This check is over registered records; it is not a garbage collector for unreferenced files on disk.

## 7. ReplaceAll is not repeated Insert

**ReplaceAll(records)** builds a completely separate candidate ID map and candidate path map, normalizing every record and rejecting duplicate IDs/portable paths. It then validates dependencies against the **complete candidate graph**, so arbitrary record ordering in a manifest does not cause false missing-reference errors.

Only after all candidates pass does it acquire an exclusive lock and swap both live maps. This is the registry's strong replacement boundary: a malformed or inconsistent manifest must not expose a half-loaded project.

By contrast, calling Insert for each manifest record would require ordered dependencies and could leave a partially loaded registry if a later entry failed.

**Exercise:** start with a valid two-record registry. Load a candidate containing three records where the final record has a missing dependency. Verify the existing registry remains unchanged. Then load a valid shuffled candidate and verify the complete replacement succeeds.

## 8. Why read APIs return copies

**Find**, **At**, **FindByPath** and **Resolve** take a shared lock or call another read-copy method. They return AssetMetadata by value or optional value, not a reference to a mutable unordered_map element. This matters because a concurrent **Remove** or **ReplaceAll** can invalidate internal iterators/references.

**Snapshot** copies all metadata and sorts the result by AssetID. This deterministic order is important for source-controlled text manifests and reproducible independent tests; it does not imply that hash-table iteration order itself is stable.

A shared/exclusive mutex protects these metadata tables, not importer byte decoding, native renderer uploads or external filesystem writes.

## 9. Where atomicity ends

**AssetManifest::SaveAssetManifest** stages text into a temporary sibling before replacing the destination; **ReplaceAll** is an in-memory transaction. Neither action is a distributed transaction that covers every glTF, texture, audio file, derived cache key and GPU allocation.

The derived cache keys are based on content fingerprint, importer identity/version, asset type and canonical settings; those are **derived CPU artifacts**, not the source registry. [KairoRenderer's AssetAdapters](https://github.com/swayam8624/KairoRenderer/blob/main/AssetAdapters.cppm) then maps stable authored references to process-local uploaded handles.

**Exercise:** change only the importer version while leaving the AssetID and source bytes fixed. Explain why the derived artifact key can change without changing authored identity.

## 10. Test-driven reading and remaining questions

Read [AssetTests.cpp](../tests/AssetTests.cpp) and the import/manifest/cache tests in [SOURCE_READING_ORDER.md](SOURCE_READING_ORDER.md). Recommended regression cases: duplicate folded paths, incorrect typed references, missing targets, cyclic dependency chain, failed Create/Insert allocation, case-only Move, Move revision exhaustion, ReplaceAll failure rollback, concurrent readers during ReplaceAll, and re-import provenance invalidation.

A complete license/release review also checks imported models, textures, sound and fonts separately. Stable identity and SHA-256 content integrity do not grant intellectual-property redistribution permission.

*Evidence note: this is a source-reading and exercise guide. No new test run or manifest stress benchmark is claimed.*
