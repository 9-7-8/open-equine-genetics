//! Open Equine Genetics: a small reader for the genes/ and breeds/ JSON files.
//!
//! ```no_run
//! use open_equine_genetics::Database;
//! let db = Database::load("path/to/open-equine-genetics").unwrap();
//! let e = db.gene("extension").unwrap().outcome("E", "e", None).unwrap();
//! println!("{}", e.phenotype.unwrap().name); // "Wild type"
//! ```
//!
//! The API is the same in every language in apis/; apis/README.md describes it.

use serde::Deserialize;
use serde_json::Value;
use std::collections::BTreeMap;
use std::error::Error;
use std::fmt;
use std::fs;
use std::path::Path;

/// Two allele ids. Order within a pair carries no meaning.
pub type Pair = (String, String);

pub const GENERAL_POPULATION: &str = "general population";

/// True if two allele pairs are the same pair, in either order.
pub fn same_pair(x: &Pair, y: &Pair) -> bool {
    (x.0 == y.0 && x.1 == y.1) || (x.0 == y.1 && x.1 == y.0)
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Sex {
    Mare,
    Stallion,
}

impl Sex {
    pub fn as_str(self) -> &'static str {
        match self {
            Sex::Mare => "mare",
            Sex::Stallion => "stallion",
        }
    }
}

impl fmt::Display for Sex {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        f.write_str(self.as_str())
    }
}

#[derive(Debug, Clone, Deserialize)]
pub struct Allele {
    pub id: String,
    pub name: String,
    pub wild_type: bool,
    /// "verified", "unverified", "abstracted", "invalid"; None on a placeholder
    pub quality: Option<String>,
    /// set only on the stand-in "Y" of an x-linked locus
    pub placeholder: Option<String>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Phenotype {
    pub id: String,
    pub name: String,
    pub description: String,
    pub changes_coat: bool,
    #[serde(default = "yes")]
    pub documented: bool,
}

fn yes() -> bool {
    true
}

#[derive(Debug, Clone, Deserialize)]
pub struct Condition {
    pub id: String,
    pub name: String,
    pub description: String,
    /// "informational", "impairing", "lethal_in_early_life", "lethal_at_birth", "lethal_at_conception"
    pub severity: String,
}

#[derive(Debug, Clone, Deserialize)]
pub struct GenotypeShare {
    pub alleles: Pair,
    pub percent: f64,
}

/// A frequency table (in a gene file) or a gene pool (in a breed file).
#[derive(Debug, Clone, Deserialize)]
pub struct GenotypeTable {
    #[serde(default)]
    pub locus: String,
    /// who a gene file's table describes; None on a breed pool
    #[serde(default)]
    pub population: Option<String>,
    /// "published" or "estimated"
    pub evidence: String,
    pub genotypes: Vec<GenotypeShare>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Citation {
    pub source: String,
    pub label: String,
    pub url: String,
    pub verified: bool,
}

/// One row of a gene file's "genotypes" or "nonviable_genotypes".
#[derive(Debug, Clone, Deserialize)]
pub struct GenotypeRow {
    pub alleles: Pair,
    pub phenotype: Option<String>,
    #[serde(default)]
    pub conditions: Vec<String>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Interactions {
    pub depends_on: Vec<String>,
    pub table: Vec<InteractionRow>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct InteractionRow {
    pub alleles: Pair,
    /// the pairs at the other loci, in the file's key order
    #[serde(deserialize_with = "ordered_pairs")]
    pub with: Vec<(String, Pair)>,
    pub phenotype: String,
}

fn ordered_pairs<'de, D: serde::Deserializer<'de>>(d: D) -> Result<Vec<(String, Pair)>, D::Error> {
    let map = serde_json::Map::<String, Value>::deserialize(d)?;
    map.into_iter()
        .map(|(k, v)| Ok((k, serde_json::from_value(v).map_err(serde::de::Error::custom)?)))
        .collect()
}

/// What one allele pair at one locus does to a horse.
#[derive(Debug, Clone)]
pub struct Outcome<'a> {
    /// as the gene file writes the pair
    pub alleles: Pair,
    /// None only for a nonviable pair with no phenotype given
    pub phenotype: Option<&'a Phenotype>,
    pub conditions: Vec<&'a Condition>,
    /// false: no living horse has this pair
    pub viable: bool,
}

#[derive(Debug, Clone)]
pub struct Offspring<'a> {
    pub alleles: Pair,
    /// 0 to 1
    pub probability: f64,
    /// None only if the pair is not in the gene file
    pub outcome: Option<Outcome<'a>>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Gene {
    pub id: String,
    pub name: String,
    pub symbol: Option<String>,
    pub chromosome: Option<String>,
    /// "published" or "abstracted" (an abstracted locus is not a real gene)
    pub evidence: String,
    pub summary: String,
    /// "autosomal", "x_linked", "y_linked"
    pub inheritance: String,
    /// changes the hair coat
    pub affects_coat: bool,
    /// changes how the eye is drawn; false where not given
    #[serde(default)]
    pub affects_eye: bool,
    pub alleles: Vec<Allele>,
    pub phenotypes: Vec<Phenotype>,
    #[serde(default)]
    pub conditions: Vec<Condition>,
    pub genotypes: Vec<GenotypeRow>,
    #[serde(default)]
    pub nonviable_genotypes: Vec<GenotypeRow>,
    pub interactions: Option<Interactions>,
    pub frequencies: Vec<GenotypeTable>,
    #[serde(default)]
    pub citations: Vec<Citation>,
    /// the whole file, for fields this reader does not model
    #[serde(skip)]
    pub raw: Value,
}

impl Gene {
    pub fn allele(&self, id: &str) -> Option<&Allele> {
        self.alleles.iter().find(|a| a.id == id)
    }

    pub fn phenotype(&self, id: &str) -> Option<&Phenotype> {
        self.phenotypes.iter().find(|p| p.id == id)
    }

    pub fn condition(&self, id: &str) -> Option<&Condition> {
        self.conditions.iter().find(|c| c.id == id)
    }

    pub fn wild_type(&self) -> &Allele {
        self.alleles.iter().find(|a| a.wild_type).expect("a locus has a wild-type allele")
    }

    pub fn placeholder(&self) -> Option<&Allele> {
        self.alleles.iter().find(|a| a.placeholder.as_deref().is_some_and(|p| !p.is_empty()))
    }

    /// The outcome of the pair a/b, or None if it is not a pair of this locus.
    ///
    /// context maps other locus ids to their pairs. Where this locus depends on others
    /// (its "interactions") and context gives all of them, the interaction table decides
    /// the phenotype; otherwise the result is the outcome with the other loci at wild type.
    pub fn outcome(&self, a: &str, b: &str, context: Option<&BTreeMap<String, Pair>>) -> Option<Outcome<'_>> {
        let pair = (a.to_string(), b.to_string());
        if let Some(row) = self.genotypes.iter().find(|r| same_pair(&r.alleles, &pair)) {
            return Some(self.row_outcome(row, true, context));
        }
        self.nonviable_genotypes
            .iter()
            .find(|r| same_pair(&r.alleles, &pair))
            .map(|row| self.row_outcome(row, false, context))
    }

    fn row_outcome(&self, row: &GenotypeRow, viable: bool, context: Option<&BTreeMap<String, Pair>>) -> Outcome<'_> {
        let mut phenotype_id = row.phenotype.as_deref();
        if let (true, Some(interactions), Some(context)) = (viable, &self.interactions, context) {
            if !context.is_empty() && interactions.depends_on.iter().all(|l| context.contains_key(l)) {
                let hit = interactions.table.iter().find(|entry| {
                    same_pair(&entry.alleles, &row.alleles)
                        && entry.with.iter().all(|(locus, p)| context.get(locus).is_some_and(|c| same_pair(p, c)))
                });
                if let Some(entry) = hit {
                    phenotype_id = Some(&entry.phenotype);
                }
            }
        }
        Outcome {
            alleles: row.alleles.clone(),
            phenotype: phenotype_id.filter(|id| !id.is_empty()).and_then(|id| self.phenotype(id)),
            conditions: row.conditions.iter().filter_map(|c| self.condition(c)).collect(),
            viable,
        }
    }

    /// Every pair a foal of this dam and sire can get at this locus, with its chance.
    ///
    /// Each parent passes one of its two alleles, each with chance 1/2. On an x-linked
    /// locus the sire's placeholder "Y" stands for passing his Y chromosome: those foals
    /// are colts. Nonviable pairs are included, marked by outcome.viable.
    pub fn cross(&self, dam: &Pair, sire: &Pair) -> Vec<Offspring<'_>> {
        let mut result: Vec<Offspring> = Vec::new();
        for x in [&dam.0, &dam.1] {
            for y in [&sire.0, &sire.1] {
                let pair = (x.clone(), y.clone());
                if let Some(existing) = result.iter_mut().find(|o| same_pair(&o.alleles, &pair)) {
                    existing.probability += 0.25;
                    continue;
                }
                let outcome = self.outcome(x, y, None);
                let alleles = outcome.as_ref().map_or(pair, |o| o.alleles.clone());
                result.push(Offspring { alleles, probability: 0.25, outcome });
            }
        }
        result
    }

    /// One of the locus's frequency tables; None asks for the "general population" one.
    pub fn frequency_table(&self, population: Option<&str>) -> Option<&GenotypeTable> {
        let population = population.unwrap_or(GENERAL_POPULATION);
        self.frequencies.iter().find(|t| t.population.as_deref() == Some(population))
    }
}

#[derive(Debug, Clone, Deserialize)]
pub struct Strain {
    pub name: String,
    pub share: f64,
    pub pools: Vec<GenotypeTable>,
}

#[derive(Debug, Clone, Deserialize)]
pub struct Breed {
    pub id: String,
    pub name: String,
    pub country: String,
    pub description: Option<String>,
    /// estimated 1-10 scores, not measurements; evidence is always "abstracted"
    pub scores: Value,
    pub pools: Vec<GenotypeTable>,
    #[serde(default)]
    pub strains: Vec<Strain>,
    #[serde(default)]
    pub citations: Vec<Citation>,
    #[serde(skip)]
    pub raw: Value,
}

impl Breed {
    pub fn strain(&self, name: &str) -> Option<&Strain> {
        self.strains.iter().find(|s| s.name == name)
    }

    /// The breed's gene pool at a locus: the strain's if it has one there, else the breed's.
    pub fn pool(&self, locus: &str, strain: Option<&str>) -> Option<&GenotypeTable> {
        let s = strain.filter(|s| !s.is_empty()).and_then(|s| self.strain(s));
        s.and_then(|s| s.pools.iter().find(|p| p.locus == locus))
            .or_else(|| self.pools.iter().find(|p| p.locus == locus))
    }
}

#[derive(Debug, Clone)]
pub struct Horse {
    pub sex: Sex,
    pub breed: Option<String>,
    pub strain: Option<String>,
    /// locus id -> pair, every locus in the database
    pub genotype: BTreeMap<String, Pair>,
}

/// The index drawn from a list of weights, by the sampling rule in apis/README.md.
fn pick(weights: &[f64], rng: &mut impl FnMut() -> f64) -> Option<usize> {
    let total: f64 = weights.iter().fold(0.0, |sum, w| sum + w);
    if total <= 0.0 {
        return None;
    }
    let target = rng() * total;
    let mut running = 0.0;
    for (i, w) in weights.iter().enumerate() {
        running += w;
        if target < running {
            return Some(i);
        }
    }
    Some(weights.len() - 1)
}

/// Draw one pair from a table. rng returns a number in [0, 1).
pub fn sample<'a>(table: &'a GenotypeTable, rng: &mut impl FnMut() -> f64) -> Option<&'a Pair> {
    let weights: Vec<f64> = table.genotypes.iter().map(|g| g.percent).collect();
    pick(&weights, rng).map(|i| &table.genotypes[i].alleles)
}

/// A tiny seedable random number generator (SplitMix64), so no other crate is needed.
///
/// ```
/// let mut rng = open_equine_genetics::SplitMix64::new(42);
/// let u = rng.next_f64(); // in [0, 1)
/// ```
#[derive(Debug, Clone)]
pub struct SplitMix64 {
    state: u64,
}

impl SplitMix64 {
    pub fn new(seed: u64) -> Self {
        SplitMix64 { state: seed }
    }

    pub fn next_u64(&mut self) -> u64 {
        self.state = self.state.wrapping_add(0x9E37_79B9_7F4A_7C15);
        let mut z = self.state;
        z = (z ^ (z >> 30)).wrapping_mul(0xBF58_476D_1CE4_E5B9);
        z = (z ^ (z >> 27)).wrapping_mul(0x94D0_49BB_1331_11EB);
        z ^ (z >> 31)
    }

    pub fn next_f64(&mut self) -> f64 {
        (self.next_u64() >> 11) as f64 / (1u64 << 53) as f64
    }
}

/// An error from Database::load: which file, and what went wrong.
#[derive(Debug)]
pub struct LoadError {
    pub path: String,
    pub source: Box<dyn Error + Send + Sync>,
}

impl fmt::Display for LoadError {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}: {}", self.path, self.source)
    }
}

impl Error for LoadError {
    fn source(&self) -> Option<&(dyn Error + 'static)> {
        Some(self.source.as_ref())
    }
}

pub struct Database {
    /// id -> Gene, sorted by id
    pub genes: BTreeMap<String, Gene>,
    /// id -> Breed, sorted by id
    pub breeds: BTreeMap<String, Breed>,
}

impl Database {
    /// Read every file in <root>/genes and <root>/breeds. root is the repository folder.
    pub fn load(root: impl AsRef<Path>) -> Result<Database, LoadError> {
        let root = root.as_ref();
        let mut genes = BTreeMap::new();
        for (path, raw) in read_dir(&root.join("genes"))? {
            let mut gene: Gene = serde_json::from_value(raw.clone()).map_err(|e| err(&path, e))?;
            for t in &mut gene.frequencies {
                t.locus = gene.id.clone();
            }
            gene.raw = raw;
            genes.insert(gene.id.clone(), gene);
        }
        let mut breeds = BTreeMap::new();
        for (path, raw) in read_dir(&root.join("breeds"))? {
            let mut breed: Breed = serde_json::from_value(raw.clone()).map_err(|e| err(&path, e))?;
            for t in breed.pools.iter_mut().chain(breed.strains.iter_mut().flat_map(|s| s.pools.iter_mut())) {
                t.population = None;
            }
            breed.raw = raw;
            breeds.insert(breed.id.clone(), breed);
        }
        Ok(Database { genes, breeds })
    }

    pub fn gene(&self, id: &str) -> Option<&Gene> {
        self.genes.get(id)
    }

    pub fn breed(&self, id: &str) -> Option<&Breed> {
        self.breeds.get(id)
    }

    /// Where a horse's pair at a locus is drawn from: the breed (or strain) pool,
    /// else the gene's general-population table, else None.
    pub fn genotype_table(&self, locus: &str, breed: Option<&str>, strain: Option<&str>) -> Option<&GenotypeTable> {
        breed
            .and_then(|b| self.breeds.get(b))
            .and_then(|b| b.pool(locus, strain))
            .or_else(|| self.genes.get(locus).and_then(|g| g.frequency_table(None)))
    }

    /// A random horse: a pair at every locus. With no breed, the general population.
    ///
    /// If the breed has strains and none is named, one is drawn by share. A locus with
    /// no table gets the wild-type pair.
    pub fn random_horse(&self, breed: Option<&str>, sex: Sex, strain: Option<&str>, rng: &mut impl FnMut() -> f64) -> Horse {
        let mut strain = strain.map(str::to_string);
        if let Some(b) = breed.and_then(|b| self.breeds.get(b)) {
            if !b.strains.is_empty() && strain.is_none() {
                let shares: Vec<f64> = b.strains.iter().map(|s| s.share).collect();
                strain = pick(&shares, rng).map(|i| b.strains[i].name.clone());
            }
        }
        let mut genotype = BTreeMap::new();
        for (locus, gene) in &self.genes {
            let table = self.genotype_table(locus, breed, strain.as_deref());
            genotype.insert(locus.clone(), draw(gene, sex, table, rng));
        }
        Horse { sex, breed: breed.map(str::to_string), strain, genotype }
    }

    /// The outcome at every locus in a genotype, with interactions between loci applied.
    pub fn outcomes(&self, genotype: &BTreeMap<String, Pair>) -> BTreeMap<String, Option<Outcome<'_>>> {
        genotype
            .iter()
            .filter_map(|(locus, p)| {
                let gene = self.genes.get(locus)?;
                Some((locus.clone(), gene.outcome(&p.0, &p.1, Some(genotype))))
            })
            .collect()
    }
}

fn draw(gene: &Gene, sex: Sex, table: Option<&GenotypeTable>, rng: &mut impl FnMut() -> f64) -> Pair {
    let placeholder = gene.placeholder();
    if let Some(table) = table {
        // On an x-linked locus a mare draws from the rows without the placeholder, a stallion from those with it.
        let rows: Vec<&GenotypeShare> = table
            .genotypes
            .iter()
            .filter(|g| match placeholder {
                Some(ph) => (g.alleles.0 == ph.id || g.alleles.1 == ph.id) == (sex == Sex::Stallion),
                None => true,
            })
            .collect();
        let weights: Vec<f64> = rows.iter().map(|g| g.percent).collect();
        if let Some(i) = pick(&weights, rng) {
            return rows[i].alleles.clone();
        }
    }
    let wild = gene.wild_type().id.clone();
    match placeholder {
        Some(ph) if sex == Sex::Stallion => (wild, ph.id.clone()),
        _ => (wild.clone(), wild),
    }
}

fn err(path: &Path, e: impl Into<Box<dyn Error + Send + Sync>>) -> LoadError {
    LoadError { path: path.display().to_string(), source: e.into() }
}

fn read_dir(dir: &Path) -> Result<Vec<(std::path::PathBuf, Value)>, LoadError> {
    let mut names: Vec<String> = fs::read_dir(dir)
        .map_err(|e| err(dir, e))?
        .filter_map(|entry| entry.ok()?.file_name().into_string().ok())
        .filter(|name| name.ends_with(".json"))
        .collect();
    names.sort();
    names
        .into_iter()
        .map(|name| {
            let path = dir.join(name);
            let text = fs::read_to_string(&path).map_err(|e| err(&path, e))?;
            let value = serde_json::from_str(&text).map_err(|e| err(&path, e))?;
            Ok((path, value))
        })
        .collect()
}
