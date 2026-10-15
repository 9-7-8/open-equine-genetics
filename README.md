# Open Equine Genetics

Real horse genetics as plain JSON, for any game, mod, app or study that wants it: 68 loci
(coat colour, white patterns, size, health) and 121 natural breeds. One file per gene, one
file per breed. Any language with a JSON parser can read it, and there are ready-made
readers for five.

## Start here

**1. Get the data.** Clone or download this repository. Everything you need is in
`genes/` and `breeds/`.

**2. Pick a reader**, or read the JSON yourself. Each reader in [apis/](apis/) is a single
file (or one small crate) to copy into your project, and they all have the same calls:

| Language | Reader | Needs |
| --- | --- | --- |
| Python | [apis/python/](apis/python/) | nothing beyond Python 3.8 |
| Java | [apis/java/](apis/java/) | nothing beyond Java 17 |
| Rust | [apis/rust/](apis/rust/) | `serde_json` |
| C++ | [apis/cpp/](apis/cpp/) | nothing beyond C++17 (one header) |
| C | [apis/c/](apis/c/) | nothing beyond C99 |

**3. Ask it things.** In Python:

```python
from open_equine_genetics import Database, MARE

db = Database.load("path/to/open-equine-genetics")

db.gene("extension").outcome("e", "e").phenotype.name        # "Chestnut"

for foal in db.gene("extension").cross(("E", "e"), ("E", "e")):
    print(foal.alleles, foal.probability)                    # E/E 0.25, E/e 0.5, e/e 0.25

horse = db.random_horse("arabian", MARE)                     # a pair at every locus
for locus, o in db.outcomes(horse.genotype).items():
    if o.phenotype.changes_coat:
        print(locus, o.phenotype.name)                       # agouti Bay, grey Fast grey, ...
```

The other languages do the same with their own spelling; [apis/README.md](apis/README.md)
lists every call.

## How far to trust it

**Nothing here has been audited yet.** It began as an export of what one game models (the
Minecraft mod [Procedural Horse Coats](https://github.com/9-7-8/Procedural-Horse-Coats))
and is being checked locus by locus against UC Davis Veterinary Genetics Laboratory, OMIA
and the primary papers. Every entry says how far to trust it, in one of three words:

| Word | Meaning |
| --- | --- |
| `published` | A cited source says so. On a locus: the locus is real and identified. |
| `estimated` | A number of ours for something real. **Most frequencies, and every breed pool, are estimates**: they were tuned for a game, not measured. |
| `abstracted` | A stand-in of ours for something real that is polygenic or not understood (height, sooty, flaxen). Its alleles are invented. It is not a real gene. |

Each allele also carries a `quality`: `verified`, `unverified`, `abstracted`, or
`invalid` (kept on purpose so a discredited allele is not added back; never make horses
with it). Each citation says whether a maintainer has `verified` it. Today almost nothing
is verified.

What is known to be wrong or unproven is listed in
[knowledge/known-issues.txt](knowledge/known-issues.txt).

## What is in a gene file

[genes/extension.json](genes/extension.json) is the simplest one. In short:

```jsonc
{
  "id": "extension", "symbol": "MC1R", "chromosome": "3",
  "evidence": "published", "inheritance": "autosomal",
  "alleles":    [ { "id": "E", "wild_type": true, "quality": "unverified" }, { "id": "e", ... } ],
  "phenotypes": [ { "id": "chestnut", "name": "Chestnut", "changes_coat": true, ... } ],
  "genotypes":  [ { "alleles": ["e", "e"], "phenotype": "chestnut" }, ... ],   // every possible pair, once
  "nonviable_genotypes": [ ... ],      // pairs no living horse has
  "conditions": [ ... ],               // health conditions, each with a severity
  "interactions": { ... },             // where the outcome depends on other loci
  "frequencies": [ { "population": "general population", "evidence": "estimated", "genotypes": [ ... ] } ],
  "citations":  [ { "source": "ucdavis-vgl", "url": "...", "verified": false } ]
}
```

A breed file ([breeds/arabian.json](breeds/arabian.json)) holds the breed's gene pools in
the same shape as a frequency table, and optional strains whose pools replace the
breed's at some loci. Every field is described in [knowledge/format.txt](knowledge/format.txt).

Allele ids and their order are permanent: new alleles are appended, never renamed,
reordered or removed, so a horse you save today still reads tomorrow.

## What is where

| Path | What it is |
| --- | --- |
| `genes/` | 68 loci, one JSON file each: 42 published, 26 abstracted. |
| `breeds/` | 121 natural breeds, one JSON file each: gene pools and the game's 1-10 scores, all estimates. |
| `apis/` | Readers in Python, Java, Rust, C++ and C, with examples. |
| `knowledge/format.txt` | The file format, field by field, and the rules a change must keep. |
| `knowledge/known-issues.txt` | What is known to be wrong, unproven or still being researched. |
| `knowledge/science/` | A science note per gene. A secondary source. |
| `knowledge/` | Everything else a maintainer needs; start at `knowledge/START-HERE.txt`. |
| `tools/validate.mjs` | Checks every gene and breed file against the format: `node tools/validate.mjs` (Node 22+, no packages). |
| `mod-map.json` | How each locus maps to the Minecraft mod's saved gene keys. Only the mod needs it. |

## Licence

GPL 3 or later (`GPL-3.0-or-later`). See [LICENSE](LICENSE). A contributor guide is not written yet.
