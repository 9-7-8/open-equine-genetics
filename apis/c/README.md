# C reader

C99, standard library only: one header, one source file, a small JSON parser inside.
Copy `open_equine_genetics.h` and `open_equine_genetics.c` into your project and compile
the `.c` with the rest of your code. The calls are those of [../README.md](../README.md).

Build the example and the conformance report (run from this folder):

```sh
cc -std=c99 -O2 -o example open_equine_genetics.c example.c
cc -std=c99 -O2 -o conformance open_equine_genetics.c conformance.c
./example ../..
```

With MSVC, from a Developer Command Prompt:

```bat
cl /W4 /O2 open_equine_genetics.c example.c /Fe:example.exe
example.exe ..\..
```

```c
char err[256];
oeg_database *db = oeg_load("path/to/open-equine-genetics", err, sizeof err);
const oeg_gene *ext = oeg_gene_find(db, "extension");
oeg_pair_outcome o;
if (oeg_outcome(ext, "E", "e", NULL, &o)) puts(o.phenotype->name);   /* Wild type */

uint64_t seed = 42;
oeg_horse mare;
oeg_random_horse(db, "arabian", OEG_MARE, NULL, oeg_default_rng, &seed, &mare);
oeg_horse_free(&mare);
oeg_free(db);
```

Every string and array belongs to the database and lives until `oeg_free`. Structs are
plain: read `gene->alleles[i].id` and `gene->allele_count` directly. Numbers are read with
`strtod`, so leave the C locale's decimal point as `.`.
