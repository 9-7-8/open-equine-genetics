/* Prints the conformance report described in apis/README.md. Every port prints the same.

       conformance ../..
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "open_equine_genetics.h"

static const char *phen(const oeg_pair_outcome *o)
{
    return o != NULL && o->phenotype != NULL ? o->phenotype->id : "-";
}

static int has(oeg_pair p, const char *allele)
{
    return strcmp(p.a, allele) == 0 || strcmp(p.b, allele) == 0;
}

static double half(void *state)
{
    (void)state;
    return 0.5;
}

/* The cross: the dam is the first mare's pair, the sire the last stallion's, heterozygous
   where possible. */
static void print_cross(const oeg_gene *g)
{
    const oeg_allele *ph = oeg_gene_placeholder(g);
    const oeg_pair *dam = NULL, *dam_any = NULL, *sire = NULL, *sire_any = NULL;
    oeg_offspring kids[4];
    size_t i, n;
    for (i = 0; i < g->genotype_count; i++) {
        const oeg_pair *p = &g->genotypes[i].alleles;
        int het = strcmp(p->a, p->b) != 0;
        if (ph == NULL || !has(*p, ph->id)) {
            if (dam_any == NULL) dam_any = p;
            if (het && dam == NULL) dam = p;
        }
        if (ph == NULL || has(*p, ph->id)) {
            sire_any = p;
            if (het) sire = p;
        }
    }
    if (dam == NULL) dam = dam_any;
    if (sire == NULL) sire = sire_any;
    if (dam == NULL || sire == NULL)
        return;
    printf("  cross %s/%s x %s/%s\n", dam->a, dam->b, sire->a, sire->b);
    n = oeg_cross(g, *dam, *sire, kids, 4);
    for (i = 0; i < n; i++)
        printf("    %s/%s %.6f %s\n", kids[i].alleles.a, kids[i].alleles.b, kids[i].probability,
               kids[i].found ? phen(&kids[i].outcome) : "-");
}

static void print_gene(const oeg_gene *g)
{
    size_t i, k;
    printf("gene %s %s %s alleles=%lu\n", g->id, g->inheritance, g->evidence, (unsigned long)g->allele_count);
    for (i = 0; i < g->genotype_count + g->nonviable_count; i++) {
        const oeg_genotype_row *row = i < g->genotype_count ? &g->genotypes[i]
                                                            : &g->nonviable_genotypes[i - g->genotype_count];
        oeg_pair_outcome o;
        oeg_outcome(g, row->alleles.b, row->alleles.a, NULL, &o);
        printf("  outcome %s/%s -> %s [", o.alleles.a, o.alleles.b, phen(&o));
        for (k = 0; k < o.condition_count; k++)
            printf("%s%s", k ? "," : "", o.conditions[k]->id);
        printf("] viable=%s\n", o.viable ? "true" : "false");
    }
    for (i = 0; i < g->interaction_count; i++) {
        const oeg_interaction_row *e = &g->interactions[i];
        oeg_genotype context;
        oeg_pair_outcome o;
        context.pairs = e->with;
        context.count = e->with_count;
        oeg_outcome(g, e->alleles.a, e->alleles.b, &context, &o);
        printf("  with ");
        for (k = 0; k < e->with_count; k++)
            printf("%s%s=%s/%s", k ? "," : "", e->with[k].locus, e->with[k].pair.a, e->with[k].pair.b);
        printf(" : %s/%s -> %s\n", o.alleles.a, o.alleles.b, phen(&o));
    }
    print_cross(g);
}

static int print_horse(const oeg_database *db, const oeg_breed *b, oeg_sex sex)
{
    oeg_horse horse;
    oeg_pair_outcome *outcomes;
    size_t i;
    if (!oeg_random_horse(db, b->id, sex, NULL, half, NULL, &horse))
        return 0;
    outcomes = (oeg_pair_outcome *)malloc((horse.genotype.count + 1) * sizeof *outcomes);
    if (outcomes == NULL) {
        oeg_horse_free(&horse);
        return 0;
    }
    oeg_outcomes(db, &horse.genotype, outcomes);
    printf("  horse %s strain=%s\n", horse.sex == OEG_MARE ? "mare" : "stallion", horse.strain ? horse.strain : "-");
    for (i = 0; i < horse.genotype.count; i++) {
        const oeg_locus_pair *lp = &horse.genotype.pairs[i];
        printf("    %s %s/%s %s\n", lp->locus, lp->pair.a, lp->pair.b, phen(&outcomes[i]));
    }
    free(outcomes);
    oeg_horse_free(&horse);
    return 1;
}

int main(int argc, char **argv)
{
    char err[512];
    oeg_database *db = oeg_load(argc > 1 ? argv[1] : "../..", err, sizeof err);
    size_t i;
    int ok = 1;
    if (db == NULL) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    for (i = 0; i < db->gene_count; i++)
        print_gene(&db->genes[i]);
    for (i = 0; i < db->breed_count && ok; i++) {
        const oeg_breed *b = &db->breeds[i];
        printf("breed %s strains=%lu\n", b->id, (unsigned long)b->strain_count);
        ok = print_horse(db, b, OEG_MARE);
        if (ok && i < 3)
            ok = print_horse(db, b, OEG_STALLION);
    }
    oeg_free(db);
    if (!ok) {
        fprintf(stderr, "out of memory\n");
        return 1;
    }
    return 0;
}
