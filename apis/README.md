# Suggested APIs

Small readers for the data, one per language, each a single file (or one small crate)
you can copy into your project. They all offer the same calls, so a guide for one is a
guide for all. None is required: the data is plain JSON, and any JSON parser can read it.

| Language | Where | Needs | Run the example |
| --- | --- | --- | --- |
| Python | [python/](python/) | Python 3.8+, nothing else | `python example.py ../..` |
| Java | [java/](java/) | Java 17+, nothing else | `java Example.java ../..` |
| Rust | [rust/](rust/) | `serde_json` | `cargo run --example example -- ../..` |
| C++ | [cpp/](cpp/) | C++17, nothing else (one header) | see [cpp/README.md](cpp/README.md) |
| C | [c/](c/) | C99, nothing else (one `.h`, one `.c`) | see [c/README.md](c/README.md) |

Each reader takes the path of this repository's root folder (the one holding `genes/` and
`breeds/`) and reads every file in those two folders.

## The calls

Names follow each language's habit (`random_horse` in Python, Rust and C, `randomHorse`
in Java); what they do is the same.

| Call | What it gives |
| --- | --- |
| `Database.load(root)` | Every gene and breed. Genes and breeds are kept sorted by id. |
| `db.gene(id)`, `db.breed(id)` | One locus or breed, or nothing if there is no such id. |
| `gene.outcome(a, b)` | What the pair `a/b` does: its `phenotype`, its health `conditions`, and whether it is `viable` at all. Order within a pair does not matter. Nothing if `a/b` is not a pair of this locus. |
| `gene.outcome(a, b, context)` | The same, where `context` gives the pairs at other loci. A locus whose outcome depends on others (leopard on `patn1` and `patn2`) uses them; without them, the result is the outcome with those loci at wild type. |
| `gene.cross(dam, sire)` | Every pair a foal can get at this locus, with its chance (0 to 1) and its outcome. Nonviable pairs are included and marked. On an x-linked locus a sire's `Y` stands for passing his Y chromosome: those foals are colts. |
| `gene.frequency_table(population)` | One of the locus's frequency tables, by default the `"general population"` one. |
| `breed.pool(locus, strain)` | The breed's gene pool at a locus: the strain's, if the strain has its own there, else the breed's. |
| `db.genotype_table(locus, breed, strain)` | Where a horse's pair at a locus is drawn from: the breed's pool, else the gene's general-population table, else nothing. |
| `sample(table, rng)` | One pair drawn from a table. |
| `db.random_horse(breed, sex, strain, rng)` | A random horse of a breed (or of the general population with no breed): a pair at every locus. A breed with strains and no strain named draws one by share. A locus with no table gets the wild-type pair. |
| `db.outcomes(genotype)` | The outcome at every locus of a horse, with the loci that depend on each other worked out together. |

### Reading the answers

- An outcome's `phenotype.changes_coat` is false for every outcome that paints nothing,
  which includes every outcome of a health locus. It does not mean "healthy".
- `condition.severity` is one of `informational`, `impairing`, `lethal_in_early_life`,
  `lethal_at_birth`, `lethal_at_conception`.
- A gene with `evidence` `"abstracted"` is a stand-in for something polygenic (height,
  sooty). It is not a real gene; say so if you show it to people.
- An allele with `quality` `"invalid"` is kept only so its id is never reused. Do not make
  horses with it.
- Every frequency and breed pool is an estimate today. The main [README](../README.md)
  says how far to trust each part.

## Sampling, exactly

So that every port draws the same horse from the same random numbers:

- `rng` gives a number `u` in `[0, 1)`. `sample` adds up the table's percents into `total`,
  and returns the first row, in file order, at which the running sum of percents is
  greater than `u * total`. A table whose total is 0 gives nothing.
- `random_horse` draws first the strain (only if the breed has strains and none was named,
  using the strains' `share`s the same way), then one pair per locus in id order. A locus
  with no table, or with an empty one, uses no number.
- On an x-linked locus, a mare draws only from the rows without the placeholder allele
  and a stallion only from the rows with it. With nothing to draw from, a mare gets the
  wild type twice and a stallion the wild type and the placeholder.

## Conformance

Every port has a program that prints the same report; if two ports print different
reports, one of them is wrong. `python check.py` (in this folder) builds and runs each
port it finds a compiler for and compares them with the Python one.

The report, one line per item, `\n` line ends, UTF-8:

```
gene <id> <inheritance> <evidence> alleles=<count>
  outcome <a>/<b> -> <phenotype id or -> [<condition ids, comma-separated>] viable=<true|false>
  with <locus>=<a>/<b>,<locus>=<a>/<b> : <a>/<b> -> <phenotype id>
  cross <a>/<b> x <a>/<b>
    <a>/<b> <probability, 6 decimals> <phenotype id or ->
breed <id> strains=<count>
  horse <mare|stallion> strain=<name or ->
    <locus> <a>/<b> <phenotype id or ->
```

- Genes in id order. One `outcome` line for each row of `genotypes` and then of
  `nonviable_genotypes`, in file order, looked up with the two alleles swapped (to prove
  order does not matter) and printed as the file writes the pair.
- One `with` line per row of `interactions.table`, in file order, looked up with that
  row's `with` as the context; the `with` pairs are printed as the file writes them, in
  the file's key order.
- The cross: the dam is the first pair in `genotypes` without the placeholder, the sire
  the last pair with it (on an autosomal locus, any pair), each taken from the
  heterozygous pairs if there are any. Children in the order dam allele 1 with sire allele
  1, 1 with 2, 2 with 1, 2 with 2, a pair already listed adding its chance to the first
  listing, printed as the file writes the pair.
- Breeds in id order. For the first three breeds a mare and then a stallion, for the rest
  a mare; each is `random_horse` with no strain named and an `rng` that always gives 0.5,
  every locus in id order, with the phenotype from `outcomes`.
