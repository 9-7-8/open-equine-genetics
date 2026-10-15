"""Prints the conformance report described in apis/README.md. Every port prints the same.

    python conformance.py ../..
"""

import sys

from open_equine_genetics import MARE, STALLION, Database


def pair(p):
    return f"{p[0]}/{p[1]}"


def phen(outcome):
    return outcome.phenotype.id if outcome and outcome.phenotype else "-"


def main(root):
    db = Database.load(root)
    out = []
    for gene in db.genes.values():
        rows = gene.raw["genotypes"]
        nonviable = gene.raw.get("nonviable_genotypes", [])
        out.append(f"gene {gene.id} {gene.inheritance} {gene.evidence} alleles={len(gene.alleles)}")
        for row in rows + nonviable:
            o = gene.outcome(row["alleles"][1], row["alleles"][0])
            out.append(f"  outcome {pair(o.alleles)} -> {phen(o)} [{','.join(c.id for c in o.conditions)}]"
                       f" viable={'true' if o.viable else 'false'}")
        for entry in gene.raw.get("interactions", {}).get("table", []):
            context = dict(entry["with"])
            o = gene.outcome(entry["alleles"][0], entry["alleles"][1], context)
            out.append(f"  with {','.join(l + '=' + pair(p) for l, p in entry['with'].items())}"
                       f" : {pair(o.alleles)} -> {phen(o)}")
        placeholder = gene.placeholder()
        # The dam is the first mare's pair, the sire the last stallion's, heterozygous where possible.
        mares = [r["alleles"] for r in rows if placeholder is None or placeholder.id not in r["alleles"]]
        sires = [r["alleles"] for r in rows if placeholder is None or placeholder.id in r["alleles"]]
        dam = ([p for p in mares if p[0] != p[1]] or mares)[0]
        sire = ([p for p in sires if p[0] != p[1]] or sires)[-1]
        out.append(f"  cross {pair(dam)} x {pair(sire)}")
        for child in gene.cross(dam, sire):
            out.append(f"    {pair(child.alleles)} {child.probability:.6f} {phen(child.outcome)}")
    for i, breed in enumerate(db.breeds.values()):
        out.append(f"breed {breed.id} strains={len(breed.strains)}")
        for sex in (MARE, STALLION) if i < 3 else (MARE,):
            horse = db.random_horse(breed.id, sex, rng=lambda: 0.5)
            out.append(f"  horse {horse.sex} strain={horse.strain or '-'}")
            outcomes = db.outcomes(horse.genotype)
            for locus, p in horse.genotype.items():
                out.append(f"    {locus} {pair(p)} {phen(outcomes[locus])}")
    sys.stdout.buffer.write(("\n".join(out) + "\n").encode("utf-8"))


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else "../..")
