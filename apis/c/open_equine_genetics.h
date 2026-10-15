/*
 * Open Equine Genetics: a small reader for the genes/ and breeds/ JSON files.
 *
 * C99, standard library only. Copy this file and open_equine_genetics.c into your project.
 *
 *     char err[256];
 *     oeg_database *db = oeg_load("path/to/open-equine-genetics", err, sizeof err);
 *     oeg_pair_outcome o;
 *     if (oeg_outcome(oeg_gene_find(db, "extension"), "E", "e", NULL, &o))
 *         puts(o.phenotype->name);                              (prints "Wild type")
 *     oeg_free(db);
 *
 * The API is the same in every language in apis/; apis/README.md describes it.
 * Every string and array belongs to the database and lives until oeg_free.
 */
#ifndef OPEN_EQUINE_GENETICS_H
#define OPEN_EQUINE_GENETICS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define OEG_GENERAL_POPULATION "general population"

/* Two allele ids. Order within a pair carries no meaning. */
typedef struct {
    const char *a;
    const char *b;
} oeg_pair;

/* A pair at one locus. An oeg_genotype is an array of these: a horse, or the context of
   an outcome. */
typedef struct {
    const char *locus;
    oeg_pair pair;
} oeg_locus_pair;

typedef struct {
    const oeg_locus_pair *pairs;
    size_t count;
} oeg_genotype;

typedef struct {
    const char *id;
    const char *name;
    int wild_type;
    const char *quality;        /* "verified", "unverified", "abstracted", "invalid"; NULL on a placeholder */
    const char *placeholder;    /* set only on the stand-in "Y" of an x-linked locus */
} oeg_allele;

typedef struct {
    const char *id;
    const char *name;
    const char *description;
    int changes_coat;
    int documented;
} oeg_phenotype;

typedef struct {
    const char *id;
    const char *name;
    const char *description;
    const char *severity;       /* "informational", "impairing", "lethal_in_early_life", "lethal_at_birth", "lethal_at_conception" */
} oeg_condition;

typedef struct {
    oeg_pair alleles;
    double percent;
} oeg_share;

/* A frequency table (in a gene file) or a gene pool (in a breed file). */
typedef struct {
    const char *locus;
    const char *population;     /* who a gene file's table describes; NULL on a breed pool */
    const char *evidence;       /* "published" or "estimated" */
    const oeg_share *genotypes;
    size_t genotype_count;
} oeg_table;

typedef struct {
    const char *source;
    const char *label;
    const char *url;
    int verified;
} oeg_citation;

/* One row of a gene file's "genotypes" or "nonviable_genotypes". */
typedef struct {
    oeg_pair alleles;
    const oeg_phenotype *phenotype;             /* NULL if the row gives none */
    const oeg_condition *const *conditions;
    size_t condition_count;
} oeg_genotype_row;

/* One row of a gene file's "interactions.table". */
typedef struct {
    oeg_pair alleles;
    const oeg_locus_pair *with;                 /* the pairs at the other loci, in file order */
    size_t with_count;
    const oeg_phenotype *phenotype;
} oeg_interaction_row;

typedef struct {
    const char *id;
    const char *name;
    const char *symbol;         /* NULL for every abstraction */
    const char *chromosome;     /* NULL where not given */
    const char *evidence;       /* "published" or "abstracted" (an abstracted locus is not a real gene) */
    const char *summary;
    const char *inheritance;    /* "autosomal", "x_linked", "y_linked" */
    int affects_coat;
    const oeg_allele *alleles;
    size_t allele_count;
    const oeg_phenotype *phenotypes;
    size_t phenotype_count;
    const oeg_condition *conditions;
    size_t condition_count;
    const oeg_table *frequencies;
    size_t frequency_count;
    const oeg_citation *citations;
    size_t citation_count;
    const oeg_genotype_row *genotypes;
    size_t genotype_count;
    const oeg_genotype_row *nonviable_genotypes;
    size_t nonviable_count;
    const char *const *depends_on;              /* loci this one's outcome depends on */
    size_t depends_on_count;
    const oeg_interaction_row *interactions;
    size_t interaction_count;
} oeg_gene;

/* A breed score: low == high unless the file gives a range. */
typedef struct {
    double low;
    double high;
} oeg_score;

/* Estimated 1-10 scores, not measurements; evidence is always "abstracted". */
typedef struct {
    const char *evidence;
    oeg_score speed, jump, health, pull;
} oeg_scores;

typedef struct {
    const char *name;
    double share;
    const oeg_table *pools;
    size_t pool_count;
} oeg_strain;

typedef struct {
    const char *id;
    const char *name;
    const char *country;
    const char *description;    /* NULL where not given */
    oeg_scores scores;
    const oeg_table *pools;
    size_t pool_count;
    const oeg_strain *strains;
    size_t strain_count;
    const oeg_citation *citations;
    size_t citation_count;
} oeg_breed;

typedef struct oeg_database {
    const oeg_gene *genes;      /* sorted by id */
    size_t gene_count;
    const oeg_breed *breeds;    /* sorted by id */
    size_t breed_count;
    void *internal;
} oeg_database;

/* What one allele pair at one locus does to a horse. */
typedef struct {
    oeg_pair alleles;                           /* as the gene file writes the pair */
    const oeg_phenotype *phenotype;             /* NULL only for a nonviable pair with no phenotype given */
    const oeg_condition *const *conditions;
    size_t condition_count;
    int viable;                                 /* 0: no living horse has this pair */
} oeg_pair_outcome;

typedef struct {
    oeg_pair alleles;
    double probability;                         /* 0 to 1 */
    int found;                                  /* 0 only if the pair is not in the gene file */
    oeg_pair_outcome outcome;
} oeg_offspring;

typedef enum { OEG_MARE, OEG_STALLION } oeg_sex;

typedef struct {
    oeg_sex sex;
    const char *breed;          /* the breed's id, or NULL for the general population */
    const char *strain;         /* NULL if none */
    oeg_genotype genotype;      /* one pair per locus, in id order; free with oeg_horse_free */
} oeg_horse;

/* Returns a number in [0, 1). */
typedef double (*oeg_rng)(void *state);

/* Read every file in <root>/genes and <root>/breeds. root is the repository folder.
   On failure returns NULL and writes a message to err. */
oeg_database *oeg_load(const char *root, char *err, size_t errlen);
void oeg_free(oeg_database *db);

const oeg_gene *oeg_gene_find(const oeg_database *db, const char *id);
const oeg_breed *oeg_breed_find(const oeg_database *db, const char *id);

const oeg_allele *oeg_gene_allele(const oeg_gene *gene, const char *id);
const oeg_phenotype *oeg_gene_phenotype(const oeg_gene *gene, const char *id);
const oeg_condition *oeg_gene_condition(const oeg_gene *gene, const char *id);
const oeg_allele *oeg_gene_wild_type(const oeg_gene *gene);
const oeg_allele *oeg_gene_placeholder(const oeg_gene *gene);   /* NULL if none */
const oeg_strain *oeg_breed_strain(const oeg_breed *breed, const char *name);

/* 1 if the two pairs are the same pair. */
int oeg_same_pair(oeg_pair x, oeg_pair y);

/* The outcome of the pair a/b. Returns 0 if it is not a pair of this locus.
   context (may be NULL) gives the pairs at other loci. Where this locus depends on others
   and context gives all of them, the interaction table decides the phenotype; otherwise
   the result is the outcome with the other loci at wild type. */
int oeg_outcome(const oeg_gene *gene, const char *a, const char *b,
                const oeg_genotype *context, oeg_pair_outcome *out);

/* Every pair a foal of this dam and sire can get at this locus, with its chance. At most 4;
   returns how many were written to out (no more than max). On an x-linked locus the sire's
   placeholder "Y" stands for passing his Y chromosome. Nonviable pairs are included. */
size_t oeg_cross(const oeg_gene *gene, oeg_pair dam, oeg_pair sire, oeg_offspring *out, size_t max);

/* population NULL means OEG_GENERAL_POPULATION. NULL if the gene has no such table. */
const oeg_table *oeg_frequency_table(const oeg_gene *gene, const char *population);

/* The breed's gene pool at a locus: the strain's if it has one there, else the breed's. */
const oeg_table *oeg_breed_pool(const oeg_breed *breed, const char *locus, const char *strain);

/* Where a horse's pair at a locus is drawn from: the breed (or strain) pool, else the
   gene's general-population table, else NULL. breed and strain may be NULL. */
const oeg_table *oeg_genotype_table(const oeg_database *db, const char *locus,
                                    const char *breed, const char *strain);

/* One pair drawn from a table, or NULL if its percents total 0. */
const oeg_pair *oeg_sample(const oeg_table *table, oeg_rng rng, void *rng_state);

/* A random horse: a pair at every locus. breed NULL means the general population. If the
   breed has strains and strain is NULL, one is drawn by share. A locus with no table gets
   the wild-type pair. rng NULL uses oeg_default_rng with a fixed internal seed.
   Returns 0 if out of memory. Free with oeg_horse_free. */
int oeg_random_horse(const oeg_database *db, const char *breed, oeg_sex sex, const char *strain,
                     oeg_rng rng, void *rng_state, oeg_horse *horse);
void oeg_horse_free(oeg_horse *horse);

/* The outcome at every locus of a genotype, with interactions between loci applied.
   out needs room for genotype->count entries; out[i] is for genotype->pairs[i] and is
   all zero where the locus or pair is unknown. Returns how many were found. */
size_t oeg_outcomes(const oeg_database *db, const oeg_genotype *genotype, oeg_pair_outcome *out);

/* A small seedable generator (splitmix64). state points to a uint64_t seed. */
double oeg_default_rng(void *state);

#ifdef __cplusplus
}
#endif

#endif
