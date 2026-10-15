//! Prints the conformance report described in apis/README.md. Every port prints the same.
//!
//!     cargo run --example conformance -- ../..

use open_equine_genetics::{Database, Outcome, Pair, Sex};
use std::collections::BTreeMap;
use std::io::Write;

fn pair(p: &Pair) -> String {
    format!("{}/{}", p.0, p.1)
}

fn phen(outcome: Option<&Outcome>) -> String {
    outcome.and_then(|o| o.phenotype).map_or("-".to_string(), |p| p.id.clone())
}

fn main() {
    let root = std::env::args().nth(1).unwrap_or_else(|| "../..".to_string());
    let db = Database::load(&root).unwrap_or_else(|e| panic!("{e}"));
    let mut out: Vec<String> = Vec::new();
    for gene in db.genes.values() {
        out.push(format!("gene {} {} {} alleles={}", gene.id, gene.inheritance, gene.evidence, gene.alleles.len()));
        for row in gene.genotypes.iter().chain(&gene.nonviable_genotypes) {
            let o = gene.outcome(&row.alleles.1, &row.alleles.0, None).expect("a listed pair has an outcome");
            let conditions: Vec<&str> = o.conditions.iter().map(|c| c.id.as_str()).collect();
            out.push(format!(
                "  outcome {} -> {} [{}] viable={}",
                pair(&o.alleles),
                phen(Some(&o)),
                conditions.join(","),
                o.viable
            ));
        }
        for entry in gene.interactions.iter().flat_map(|i| &i.table) {
            let context: BTreeMap<String, Pair> = entry.with.iter().cloned().collect();
            let o = gene.outcome(&entry.alleles.0, &entry.alleles.1, Some(&context));
            let with: Vec<String> = entry.with.iter().map(|(l, p)| format!("{}={}", l, pair(p))).collect();
            let alleles = o.as_ref().map_or(&entry.alleles, |o| &o.alleles);
            out.push(format!("  with {} : {} -> {}", with.join(","), pair(alleles), phen(o.as_ref())));
        }
        // The dam is the first mare's pair, the sire the last stallion's, heterozygous where possible.
        let placeholder = gene.placeholder().map(|a| a.id.as_str());
        let has = |p: &Pair| placeholder.is_some_and(|ph| p.0 == ph || p.1 == ph);
        let mares: Vec<&Pair> = gene.genotypes.iter().map(|r| &r.alleles).filter(|p| placeholder.is_none() || !has(p)).collect();
        let sires: Vec<&Pair> = gene.genotypes.iter().map(|r| &r.alleles).filter(|p| placeholder.is_none() || has(p)).collect();
        let hetero = |v: &[&Pair]| -> Vec<Pair> {
            let h: Vec<Pair> = v.iter().filter(|p| p.0 != p.1).map(|p| (*p).clone()).collect();
            if h.is_empty() { v.iter().map(|p| (*p).clone()).collect() } else { h }
        };
        let (dam, sire) = (hetero(&mares)[0].clone(), hetero(&sires).last().cloned().expect("a sire pair"));
        out.push(format!("  cross {} x {}", pair(&dam), pair(&sire)));
        for child in gene.cross(&dam, &sire) {
            out.push(format!("    {} {:.6} {}", pair(&child.alleles), child.probability, phen(child.outcome.as_ref())));
        }
    }
    for (i, breed) in db.breeds.values().enumerate() {
        out.push(format!("breed {} strains={}", breed.id, breed.strains.len()));
        let sexes: &[Sex] = if i < 3 { &[Sex::Mare, Sex::Stallion] } else { &[Sex::Mare] };
        for &sex in sexes {
            let horse = db.random_horse(Some(&breed.id), sex, None, &mut || 0.5);
            out.push(format!("  horse {} strain={}", horse.sex, horse.strain.as_deref().unwrap_or("-")));
            let outcomes = db.outcomes(&horse.genotype);
            for (locus, p) in &horse.genotype {
                out.push(format!("    {} {} {}", locus, pair(p), phen(outcomes[locus].as_ref())));
            }
        }
    }
    let mut text = out.join("\n");
    text.push('\n');
    std::io::stdout().lock().write_all(text.as_bytes()).expect("write to stdout");
}
