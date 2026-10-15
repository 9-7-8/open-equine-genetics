"""Open Equine Genetics: a small reader for the genes/ and breeds/ JSON files.

One file, standard library only, Python 3.8 or newer. Copy it into your project.

    from open_equine_genetics import Database
    db = Database.load("path/to/open-equine-genetics")
    db.gene("extension").outcome("E", "e").phenotype.name    # "Wild type"

The API is the same in every language in apis/; apis/README.md describes it.
"""

import json
import os
import random as _random
from dataclasses import dataclass, field
from typing import Callable, Dict, List, Optional, Sequence, Tuple

Pair = Tuple[str, str]

MARE = "mare"
STALLION = "stallion"
GENERAL_POPULATION = "general population"


def same_pair(x: Sequence[str], y: Sequence[str]) -> bool:
    """True if two allele pairs are the same pair. Order within a pair carries no meaning."""
    return sorted(x) == sorted(y)


@dataclass
class Allele:
    id: str
    name: str
    wild_type: bool
    quality: Optional[str]      # "verified", "unverified", "abstracted", "invalid"; None on a placeholder
    placeholder: Optional[str]  # set only on the stand-in "Y" of an x-linked locus


@dataclass
class Phenotype:
    id: str
    name: str
    description: str
    changes_coat: bool
    documented: bool


@dataclass
class Condition:
    id: str
    name: str
    description: str
    severity: str  # "informational", "impairing", "lethal_in_early_life", "lethal_at_birth", "lethal_at_conception"


@dataclass
class GenotypeShare:
    alleles: Pair
    percent: float


@dataclass
class GenotypeTable:
    """A frequency table (in a gene file) or a gene pool (in a breed file)."""
    locus: str
    population: Optional[str]   # who a gene file's table describes; None on a breed pool
    evidence: str         # "published" or "estimated"
    genotypes: List[GenotypeShare]


@dataclass
class Citation:
    source: str
    label: str
    url: str
    verified: bool


@dataclass
class Outcome:
    """What one allele pair at one locus does to a horse."""
    alleles: Pair                    # as the gene file writes the pair
    phenotype: Optional[Phenotype]   # None only for a nonviable pair with no phenotype given
    conditions: List[Condition]
    viable: bool                     # False: no living horse has this pair


@dataclass
class Offspring:
    alleles: Pair
    probability: float               # 0 to 1
    outcome: Optional[Outcome]       # None only if the pair is not in the gene file


@dataclass
class Gene:
    id: str
    name: str
    symbol: Optional[str]
    chromosome: Optional[str]
    evidence: str                    # "published" or "abstracted" (an abstracted locus is not a real gene)
    summary: str
    inheritance: str                 # "autosomal", "x_linked", "y_linked"
    affects_coat: bool
    alleles: List[Allele]
    phenotypes: List[Phenotype]
    conditions: List[Condition]
    frequencies: List[GenotypeTable]
    citations: List[Citation]
    raw: dict = field(repr=False)    # the whole file, for fields this reader does not model

    def allele(self, allele_id: str) -> Optional[Allele]:
        return next((a for a in self.alleles if a.id == allele_id), None)

    def phenotype(self, phenotype_id: str) -> Optional[Phenotype]:
        return next((p for p in self.phenotypes if p.id == phenotype_id), None)

    def condition(self, condition_id: str) -> Optional[Condition]:
        return next((c for c in self.conditions if c.id == condition_id), None)

    def wild_type(self) -> Allele:
        return next(a for a in self.alleles if a.wild_type)

    def placeholder(self) -> Optional[Allele]:
        return next((a for a in self.alleles if a.placeholder), None)

    def outcome(self, a: str, b: str, context: Optional[Dict[str, Sequence[str]]] = None) -> Optional[Outcome]:
        """The outcome of the pair a/b, or None if it is not a pair of this locus.

        context maps other locus ids to their pairs. Where this locus depends on others
        (its "interactions") and context gives all of them, the interaction table decides
        the phenotype; otherwise the result is the outcome with the other loci at wild type.
        """
        for row in self.raw["genotypes"]:
            if same_pair(row["alleles"], (a, b)):
                return self._outcome(row, True, context)
        for row in self.raw.get("nonviable_genotypes", []):
            if same_pair(row["alleles"], (a, b)):
                return self._outcome(row, False, context)
        return None

    def _outcome(self, row: dict, viable: bool, context) -> Outcome:
        phenotype_id = row.get("phenotype")
        interactions = self.raw.get("interactions")
        if viable and interactions and context and all(l in context for l in interactions["depends_on"]):
            for entry in interactions["table"]:
                if same_pair(entry["alleles"], row["alleles"]) and all(
                    locus in context and same_pair(pair, context[locus]) for locus, pair in entry["with"].items()
                ):
                    phenotype_id = entry["phenotype"]
                    break
        return Outcome(
            alleles=tuple(row["alleles"]),
            phenotype=self.phenotype(phenotype_id) if phenotype_id else None,
            conditions=[c for c in (self.condition(i) for i in row.get("conditions", [])) if c],
            viable=viable,
        )

    def cross(self, dam: Sequence[str], sire: Sequence[str]) -> List[Offspring]:
        """Every pair a foal of this dam and sire can get at this locus, with its chance.

        Each parent passes one of its two alleles, each with chance 1/2. On an x-linked
        locus the sire's placeholder "Y" stands for passing his Y chromosome: those foals
        are colts. Nonviable pairs are included, marked by outcome.viable.
        """
        result: List[Offspring] = []
        for x in dam:
            for y in sire:
                existing = next((o for o in result if same_pair(o.alleles, (x, y))), None)
                if existing:
                    existing.probability += 0.25
                    continue
                outcome = self.outcome(x, y)
                result.append(Offspring(outcome.alleles if outcome else (x, y), 0.25, outcome))
        return result

    def frequency_table(self, population: str = GENERAL_POPULATION) -> Optional[GenotypeTable]:
        return next((t for t in self.frequencies if t.population == population), None)


@dataclass
class Strain:
    name: str
    share: float
    pools: List[GenotypeTable]


@dataclass
class Breed:
    id: str
    name: str
    country: str
    description: Optional[str]
    scores: dict                     # estimated 1-10 scores, not measurements; evidence is always "abstracted"
    pools: List[GenotypeTable]
    strains: List[Strain]
    citations: List[Citation]
    raw: dict = field(repr=False)

    def strain(self, name: str) -> Optional[Strain]:
        return next((s for s in self.strains if s.name == name), None)

    def pool(self, locus: str, strain: Optional[str] = None) -> Optional[GenotypeTable]:
        """The breed's gene pool at a locus: the strain's if it has one there, else the breed's."""
        s = self.strain(strain) if strain else None
        for pools in ([s.pools] if s else []) + [self.pools]:
            for p in pools:
                if p.locus == locus:
                    return p
        return None


@dataclass
class Horse:
    sex: str                         # MARE or STALLION
    breed: Optional[str]
    strain: Optional[str]
    genotype: Dict[str, Pair]        # locus id -> pair, every locus in the database


def sample(table: GenotypeTable, rng: Callable[[], float]) -> Optional[Pair]:
    """Draw one pair from a table. rng returns a float in [0, 1)."""
    total = 0.0
    for g in table.genotypes:  # added left to right, as every port does
        total += g.percent
    if total <= 0:
        return None
    target = rng() * total
    running = 0.0
    for g in table.genotypes:
        running += g.percent
        if target < running:
            return g.alleles
    return table.genotypes[-1].alleles


class Database:
    def __init__(self, genes: Dict[str, Gene], breeds: Dict[str, Breed]):
        self.genes = genes      # id -> Gene, sorted by id
        self.breeds = breeds    # id -> Breed, sorted by id

    @staticmethod
    def load(root: str) -> "Database":
        """Read every file in <root>/genes and <root>/breeds. root is the repository folder."""
        genes = sorted((_gene(d) for d in _read_dir(os.path.join(root, "genes"))), key=lambda g: g.id)
        breeds = sorted((_breed(d) for d in _read_dir(os.path.join(root, "breeds"))), key=lambda b: b.id)
        return Database({g.id: g for g in genes}, {b.id: b for b in breeds})

    def gene(self, gene_id: str) -> Optional[Gene]:
        return self.genes.get(gene_id)

    def breed(self, breed_id: str) -> Optional[Breed]:
        return self.breeds.get(breed_id)

    def genotype_table(self, locus: str, breed: Optional[str] = None, strain: Optional[str] = None) -> Optional[GenotypeTable]:
        """Where a horse's pair at a locus is drawn from: the breed (or strain) pool,
        else the gene's general-population table, else None."""
        b = self.breeds.get(breed) if breed else None
        table = b.pool(locus, strain) if b else None
        if table is None and locus in self.genes:
            table = self.genes[locus].frequency_table()
        return table

    def random_horse(self, breed: Optional[str] = None, sex: str = MARE, strain: Optional[str] = None,
                     rng: Optional[Callable[[], float]] = None) -> Horse:
        """A random horse: a pair at every locus. With no breed, the general population.

        If the breed has strains and none is named, one is drawn by share. A locus with
        no table gets the wild-type pair.
        """
        rng = rng or _random.random
        b = self.breeds.get(breed) if breed else None
        if b is not None and b.strains and strain is None:
            shares = GenotypeTable("", None, "estimated", [GenotypeShare((s.name, s.name), s.share) for s in b.strains])
            drawn = sample(shares, rng)
            strain = drawn[0] if drawn else None
        genotype: Dict[str, Pair] = {}
        for locus, gene in self.genes.items():
            genotype[locus] = self._draw(gene, sex, self.genotype_table(locus, breed, strain), rng)
        return Horse(sex, breed, strain, genotype)

    def _draw(self, gene: Gene, sex: str, table: Optional[GenotypeTable], rng) -> Pair:
        placeholder = gene.placeholder()
        if table is not None and placeholder is not None:
            male = sex == STALLION
            rows = [g for g in table.genotypes if (placeholder.id in g.alleles) == male]
            table = GenotypeTable(table.locus, table.population, table.evidence, rows)
        pair = sample(table, rng) if table is not None else None
        if pair is not None:
            return pair
        wild = gene.wild_type().id
        return (wild, placeholder.id) if placeholder is not None and sex == STALLION else (wild, wild)

    def outcomes(self, genotype: Dict[str, Sequence[str]]) -> Dict[str, Optional[Outcome]]:
        """The outcome at every locus in a genotype, with interactions between loci applied."""
        return {locus: self.genes[locus].outcome(pair[0], pair[1], genotype)
                for locus, pair in genotype.items() if locus in self.genes}


def _read_dir(path: str) -> List[dict]:
    out = []
    for name in sorted(os.listdir(path)):
        if name.endswith(".json"):
            with open(os.path.join(path, name), encoding="utf-8") as f:
                out.append(json.load(f))
    return out


def _table(d: dict, locus: str, population: Optional[str]) -> GenotypeTable:
    return GenotypeTable(locus, population, d["evidence"],
                         [GenotypeShare(tuple(g["alleles"]), g["percent"]) for g in d["genotypes"]])


def _citations(d: dict) -> List[Citation]:
    return [Citation(c["source"], c["label"], c["url"], c["verified"]) for c in d.get("citations", [])]


def _gene(d: dict) -> Gene:
    return Gene(
        id=d["id"], name=d["name"], symbol=d.get("symbol"), chromosome=d.get("chromosome"),
        evidence=d["evidence"], summary=d["summary"], inheritance=d["inheritance"],
        affects_coat=d["affects_coat"],
        alleles=[Allele(a["id"], a["name"], a["wild_type"], a.get("quality"), a.get("placeholder"))
                 for a in d["alleles"]],
        phenotypes=[Phenotype(p["id"], p["name"], p["description"], p["changes_coat"], p.get("documented", True))
                    for p in d["phenotypes"]],
        conditions=[Condition(c["id"], c["name"], c["description"], c["severity"]) for c in d.get("conditions", [])],
        frequencies=[_table(t, d["id"], t["population"]) for t in d["frequencies"]],
        citations=_citations(d),
        raw=d,
    )


def _breed(d: dict) -> Breed:
    return Breed(
        id=d["id"], name=d["name"], country=d["country"], description=d.get("description"),
        scores=d["scores"],
        pools=[_table(p, p["locus"], None) for p in d["pools"]],
        strains=[Strain(s["name"], s["share"], [_table(p, p["locus"], None) for p in s["pools"]])
                 for s in d.get("strains", [])],
        citations=_citations(d),
        raw=d,
    )
