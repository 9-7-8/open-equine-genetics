# Python

One file, standard library only, Python 3.8 or newer. Copy
`open_equine_genetics.py` into your project, next to your code.

```
python example.py ../..        # a first look: run from this folder
python conformance.py ../..    # the report every port must print the same
```

## The main calls

```python
from open_equine_genetics import Database, MARE, STALLION

db = Database.load("path/to/open-equine-genetics")   # the folder holding genes/ and breeds/

gene = db.gene("extension")
o = gene.outcome("E", "e")             # None if E/e is not a pair of this locus
o.phenotype.name, o.conditions, o.viable

for child in gene.cross(("E", "e"), ("e", "e")):
    print(child.alleles, child.probability, child.outcome.phenotype.name)

horse = db.random_horse("arabian", STALLION)        # pass rng=... for your own random numbers
for locus, o in db.outcomes(horse.genotype).items():
    print(locus, horse.genotype[locus], o.phenotype.name)
```

Every class is a plain dataclass. A `Gene` or `Breed` also keeps the whole file as
`.raw`, for fields this reader does not model. The full list of calls, and what each
answer means, is in [../README.md](../README.md).
