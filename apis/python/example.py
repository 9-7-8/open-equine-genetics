"""A first look at the data. Run from this folder:  python example.py ../.."""

import sys

from open_equine_genetics import MARE, Database

db = Database.load(sys.argv[1] if len(sys.argv) > 1 else "../..")
print(f"{len(db.genes)} loci, {len(db.breeds)} breeds")

# What does one pair do?
extension = db.gene("extension")
outcome = extension.outcome("e", "e")
print(f"\nextension e/e: {outcome.phenotype.name}")
print(f"  {outcome.phenotype.description}")

# A health locus: the pair, its conditions, and how serious they are.
outcome = db.gene("lamc2").outcome("jeb", "jeb")
if outcome:
    for c in outcome.conditions:
        print(f"\nlamc2 jeb/jeb: {c.name} ({c.severity})")

# What can a foal get? Two horses that each carry red, crossed at extension.
print("\nE/e x E/e at extension:")
for child in extension.cross(("E", "e"), ("E", "e")):
    print(f"  {child.alleles[0]}/{child.alleles[1]}  {child.probability:.0%}  {child.outcome.phenotype.name}")

# A random Arabian mare, and every locus that shows on her coat.
horse = db.random_horse("arabian", MARE)
print("\nA random Arabian mare:")
for locus, outcome in db.outcomes(horse.genotype).items():
    if outcome and outcome.phenotype and outcome.phenotype.changes_coat:
        print(f"  {locus:12} {'/'.join(horse.genotype[locus]):10} {outcome.phenotype.name}")
