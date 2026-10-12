package com.example.horsegenetics.common.trait;

import com.example.horsegenetics.common.genetics.AllelePair;
import com.example.horsegenetics.common.genetics.Gene;
import com.example.horsegenetics.common.genetics.Genotype;

import java.util.List;

/** Scratch helper: TraitBuilder's constructor and conditions are package-private. */
public final class ExportTraitAccess {
    public static List<Condition> conditions(Gene gene, AllelePair pair) {
        if (!(gene instanceof TraitContribution tc)) {
            return List.of();
        }
        try {
            TraitBuilder b = new TraitBuilder();
            tc.contribute(pair, Genotype.wildType().with(pair), b);
            return b.rawConditions();
        } catch (RuntimeException e) {
            System.err.println("conditions failed for " + gene.key() + " " + pair.toTokens() + ": " + e);
            return List.of();
        }
    }
}
