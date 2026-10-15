//! A short tour of the reader.
//!
//!     cargo run --example example -- ../..

use open_equine_genetics::{Database, Sex, SplitMix64};
use std::time::{SystemTime, UNIX_EPOCH};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let root = std::env::args().nth(1).unwrap_or_else(|| "../..".to_string());
    let db = Database::load(&root)?;
    println!("{} genes, {} breeds", db.genes.len(), db.breeds.len());

    // What one pair does at one locus.
    let extension = db.gene("extension").expect("the extension locus");
    if let Some(o) = extension.outcome("E", "e", None) {
        let name = o.phenotype.map_or("-", |p| p.name.as_str());
        println!("E/e at extension: {name} (viable: {})", o.viable);
    }

    // A cross at one locus: every pair a foal can get, with its chance.
    println!("\nE/e x E/e at extension:");
    let parent = ("E".to_string(), "e".to_string());
    for child in extension.cross(&parent, &parent) {
        let name = child.outcome.as_ref().and_then(|o| o.phenotype).map_or("-", |p| p.name.as_str());
        println!("  {}/{}  {:>3.0}%  {name}", child.alleles.0, child.alleles.1, child.probability * 100.0);
    }

    // A random Arabian mare, and what shows on her coat.
    let seed = SystemTime::now().duration_since(UNIX_EPOCH)?.as_nanos() as u64;
    let mut rng = SplitMix64::new(seed);
    let horse = db.random_horse(Some("arabian"), Sex::Mare, None, &mut || rng.next_f64());
    println!("\nA random Arabian mare's coat:");
    for (locus, outcome) in db.outcomes(&horse.genotype) {
        let Some(outcome) = outcome else { continue };
        let Some(phenotype) = outcome.phenotype.filter(|p| p.changes_coat) else { continue };
        let (a, b) = &horse.genotype[&locus];
        println!("  {locus:<12} {a}/{b}  {}", phenotype.name);
    }
    Ok(())
}
