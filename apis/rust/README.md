# Open Equine Genetics for Rust

A small reader for the `genes/` and `breeds/` JSON files. One library crate,
`open-equine-genetics`, needing only `serde` and `serde_json`. The calls are the same as in
every port; [../README.md](../README.md) describes them and the sampling rule.

## Depend on it

Point a path dependency at this folder, or copy the folder into your project:

```toml
[dependencies]
open-equine-genetics = { path = "path/to/open-equine-genetics/apis/rust" }
```

## Run the example

From this folder: `cargo run --example example -- ../..` (the argument is the repository
root). `cargo run --example conformance -- ../..` prints the conformance report.

## The main calls

```rust
use open_equine_genetics::{sample, Database, Sex, SplitMix64};

let db = Database::load("path/to/open-equine-genetics")?;
let extension = db.gene("extension").unwrap();
let o = extension.outcome("E", "e", None).unwrap();       // phenotype, conditions, viable
let foals = extension.cross(&("E".into(), "e".into()), &("e".into(), "e".into()));

let mut rng = SplitMix64::new(42);                         // or any FnMut() -> f64 in [0, 1)
let table = db.genotype_table("extension", Some("arabian"), None).unwrap();
let pair = sample(table, &mut || rng.next_f64());
let mare = db.random_horse(Some("arabian"), Sex::Mare, None, &mut || rng.next_f64());
let outcomes = db.outcomes(&mare.genotype);               // locus -> Option<Outcome>
```

Anything that may be missing comes back as an `Option`. Genes and breeds are kept in
`BTreeMap`s, so they iterate in id order.
