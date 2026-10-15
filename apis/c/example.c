/* A short tour of the C reader.

       example ../..
*/

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "open_equine_genetics.h"

int main(int argc, char **argv)
{
    char err[512];
    oeg_database *db = oeg_load(argc > 1 ? argv[1] : "../..", err, sizeof err);
    const oeg_gene *extension;
    oeg_pair_outcome o;
    oeg_offspring foals[4];
    oeg_pair dam = { "E", "e" }, sire = { "E", "e" };
    oeg_horse mare;
    oeg_pair_outcome *outcomes;
    uint64_t seed = (uint64_t)time(NULL);
    size_t i, n;

    if (db == NULL) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    printf("%lu genes, %lu breeds\n\n", (unsigned long)db->gene_count, (unsigned long)db->breed_count);

    /* What one pair does */
    extension = oeg_gene_find(db, "extension");
    if (extension != NULL && oeg_outcome(extension, "E", "e", NULL, &o)) {
        printf("E/e at extension: %s\n", o.phenotype->name);
        printf("  viable: %s, health conditions: %lu\n\n", o.viable ? "yes" : "no", (unsigned long)o.condition_count);
    }

    /* A cross at one locus */
    if (extension != NULL) {
        printf("E/e x E/e at extension:\n");
        n = oeg_cross(extension, dam, sire, foals, 4);
        for (i = 0; i < n; i++)
            printf("  %s/%s  %3.0f%%  %s\n", foals[i].alleles.a, foals[i].alleles.b, foals[i].probability * 100,
                   foals[i].found && foals[i].outcome.phenotype ? foals[i].outcome.phenotype->name : "-");
        printf("\n");
    }

    /* A random Arabian mare, and what her coat looks like */
    if (!oeg_random_horse(db, "arabian", OEG_MARE, NULL, oeg_default_rng, &seed, &mare)) {
        oeg_free(db);
        return 1;
    }
    outcomes = (oeg_pair_outcome *)malloc((mare.genotype.count + 1) * sizeof *outcomes);
    if (outcomes != NULL) {
        oeg_outcomes(db, &mare.genotype, outcomes);
        printf("A random Arabian mare's coat:\n");
        for (i = 0; i < mare.genotype.count; i++) {
            const oeg_locus_pair *lp = &mare.genotype.pairs[i];
            if (outcomes[i].phenotype != NULL && outcomes[i].phenotype->changes_coat)
                printf("  %-12s %s/%s  %s\n", lp->locus, lp->pair.a, lp->pair.b, outcomes[i].phenotype->name);
        }
        free(outcomes);
    }
    oeg_horse_free(&mare);
    oeg_free(db);
    return 0;
}
