# Localization data, runtime and authoring tool

Status: Planned. No localization database, runtime service or localization editor
exists in the current delivery. This plan owns F11-F14, H08-H12 and K08-K12.
The [macro roadmap](../plano-de-desenvolvimento.md) coordinates these handoffs.

## Scope and contracts

Developer tools use the shared .NET UI package's resource tables, initially English.
The game uses stable string keys and locale data inside source/cooked data bundles.
Share key/fallback/placeholder semantics, not a dependency on .NET inside the engine.
Strings authored by users are content; engine identifiers, schema fields, resource
keys and protocol codes stay invariant and are not translated.

The contract must specify locale identifiers, fallback and missing-key behavior,
plural/select formatting, typed arguments and culture-specific numbers/dates.
Never concatenate translated sentence fragments. XML/Lua authoring references keys;
editing reads source tables directly. Explicit packaging validates/cooks tables
into binary bundle data, including the full-game generated-C++ path where eligible.

The C# localization editor manages catalogs, translations, context, source references,
placeholder diagnostics and import/export. It consumes shared UI/NuGet components
and uses the real native renderer for game text preview. It does not implement
another game text rasterizer. Locale support is not proven by translating a label:
layout expansion, DPI, Unicode, fonts, shaping, fallback and missing glyphs need tests.
English is the shipped baseline; additional locales require explicit acceptance.

Every ID is one separate PR, aiming for 3-8 changed paths and about 10 maximum
including tests/build/package registration. Paths name the owning project, not a
commitment to create every listed subsystem in one change. Split before exceeding
the budget. A schema/fixture delivery is a contract, not a working translation tool.

## Implementation backlog

**F11 - Localization keys and catalog contract.** Depends: Q02.
Deliverable: versioned schema, `docs/localization.md` and contract fixtures/tests
(3-6 paths). Define tool resource tables versus game bundle catalogs, stable IDs,
context, locale/fallback rules, typed placeholders and reference integrity.
Select source formats and formatting strategy in the ADR; no runtime claimed.

**F12 - Source localization codecs and validation.** Depends: F11, R02.
Deliverable: catalog codec/validator under the asset pipeline and tests (4-7 paths).
Round-trip source data and preserve keys/translations; reject duplicate IDs,
fallback cycles, invalid placeholders and incompatible schemas. Source editing
and validation run without cooking or a compiler.

**F13 - Cooked localization bundle data.** Depends: F12, I06.
Deliverable: binary table writer/reader contract and cooking fixtures (4-8 paths).
Verify source/binary/generated-C++ parity where supported, deterministic indexes,
locale selection, incremental rebuilds and corruption rejection. Do not ship XML
for the runtime lookup path or require cooking while editing.

**F14 - Locale font resources and bundle dependencies.** Depends: F12, H11, R04.
Deliverable: locale/font manifest, dependency/cooking adapter and tests (4-7 paths).
Include approved fonts/fallback resources with provenance and explicit coverage;
validate missing glyphs and expose disk/decoded/measured costs without double counting.

**H08 - Native string catalog and lookup service.** Depends: F11, C02, R09.
Deliverable: C++20 catalog/key/fallback API under `engine/localization/`, source
provider adapter and GoogleTest cases (4-8 paths). Use stable handles/lifetimes,
locale switching and explicit missing-key results; no .NET dependency.

**H09 - Typed localized formatting.** Depends: H08.
Deliverable: native formatting adapter and fixtures (4-7 paths). Test plural/select
rules, number/date formatting, invalid arguments and fallback parity with tools.
Avoid sentence concatenation and hardcoded English plural rules.

**H10 - Localized widget bindings and reload.** Depends: H09, H12, H02.
Deliverable: native widget/string binding and Lua exposure with tests (4-8 paths).
Source-table edits refresh previews without cooking. Locale changes invalidate
measurement/layout and preserve widget state; missing keys remain diagnosable.

**H11 - Unicode font resources and glyph fallback.** Depends: F11, D06.
Deliverable: native font/glyph resource adapter and tests (4-8 paths). Specify
encoding, ownership, cache limits, fallback and missing glyph behavior; verify
representative non-ASCII/Japanese text using approved real font data.

**H12 - Native text shaping and layout integration.** Depends: H11, D06.
Deliverable: native shaping/measurement/render adapter and frame fixtures (4-8 paths).
Handle grapheme boundaries, wrapping, shaping and bidirectional layout according
to supported locale requirements; compare editor preview with the game renderer.
Select tested libraries in an ADR instead of implementing a second text engine.

**K08 - C# localization editor catalog document.** Depends: J02, F12.
Deliverable: `tools/dotnet/Raiders.Localization.Editor/` shell/document and MSTest
project (5-8 paths). Consume shared UI resources; open/save source catalogs with
dirty state and errors without requiring engine compilation or packaging.

**K09 - Translation editing and diagnostics.** Depends: K08.
Deliverable: shared-control editing views and tests (4-7 paths). Edit English and
additional locale entries, context, review state and missing/placeholder reports;
use undo/redo and preserve unknown supported metadata.

**K10 - Translation exchange and merge.** Depends: K09, F12.
Deliverable: import/export adapter and conflict tests (4-7 paths). Define the
exchange format, preserve stable keys and reviewed translations, detect stale
source text and prevent partial overwrites on invalid input.

**K11 - Localized preview and source navigation.** Depends: K09, J08, H10.
Deliverable: locale preview/navigation client and tests (4-7 paths). Render game
text through the native preview, inspect fallback/missing glyphs and navigate to
referencing assets/Lua. Do not reproduce native text rendering in C#.

**K12 - Localization developer and Designer acceptance.** Depends: K10, K11, F13, F14, X06.
Deliverable: real catalog/font fixtures, workflow acceptance and usage guide
(4-8 paths). Author, translate, preview, save/reload and explicitly package without
developer toolchains in the Designer workflow. Verify long text, missing keys,
Unicode and locale switching through both source and packaged providers.
