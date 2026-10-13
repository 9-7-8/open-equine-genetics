# Open Equine Genetics

A database of horse genes as plain JSON, one file per locus, for any game, mod or tool
that wants real equine genetics. No code is needed to read it.

## Read this before using the data

**Nothing here has been audited yet.** This is a first export of what one game models
(the Minecraft mod [Procedural Horse Coats](https://github.com/9-7-8/Procedural-Horse-Coats)),
to be checked locus by locus against UC Davis Veterinary Genetics Laboratory, OMIA and the
primary papers. Every entry says how far to trust it, in one of three words:

| Word | Meaning |
| --- | --- |
| `published` | A cited source says so. On a locus: the locus is real and identified. |
| `estimated` | A number of ours for something real. **Every frequency is estimated today**: they were tuned for a game, not measured. |
| `abstracted` | A stand-in of ours for something real that is polygenic or not understood (height, sooty, flaxen). Its alleles are invented. It is not a real gene. |

What is known to be wrong or unproven is listed in
[knowledge/known-issues.txt](knowledge/known-issues.txt).

## What is here

| Path | What it is |
| --- | --- |
| `genes/` | 68 loci, one JSON file each: 42 published, 26 abstracted. |
| `knowledge/format.txt` | The file format, field by field, and the rules a change must keep. |
| `knowledge/science/` | Science notes per gene, copied from the mod's wiki. A secondary source. |
| `knowledge/` | Everything else a maintainer needs; start at `knowledge/START-HERE.txt`. |
| `mod-map.json` | How each locus maps to the mod's saved gene keys. Only the mod needs it. |
| `tools/validate.mjs` | Checks every gene file against the format: `node tools/validate.mjs` (Node 22+, no packages). |

Breeds are planned and not here yet.

## The format in brief

Each `genes/<id>.json` lists the locus's alleles, every pair of alleles that can exist
with the phenotype it produces, pairs that cannot exist, health conditions with a
severity, genotype frequency tables, and citations. Allele ids and their order are
permanent: new alleles are appended, never renamed, reordered or removed.
[genes/extension.json](genes/extension.json) is the simplest example.

## Licence

GPL 3. See [LICENSE](LICENSE). A contributor guide is not written yet.
