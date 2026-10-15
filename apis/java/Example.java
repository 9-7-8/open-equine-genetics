/*
 * A first look at the data: one pair, one cross, one random horse.
 *
 *     java Example.java ../..                      (Java 22 or newer)
 *     javac -d out *.java && java -cp out Example ../..
 */

import java.nio.file.Path;

public class Example {
    public static void main(String[] args) {
        var db = OpenEquineGenetics.Database.load(Path.of(args.length > 0 ? args[0] : "../.."));
        System.out.println("Loaded " + db.genes().size() + " loci and " + db.breeds().size() + " breeds.");

        // What one pair does.
        var extension = db.gene("extension");
        var outcome = extension.outcome("E", "e");
        System.out.println();
        System.out.println("E/e at extension: " + outcome.phenotype().name());
        System.out.println("  " + outcome.phenotype().description());

        // A cross at one locus: every pair a foal can get, with its chance.
        var dam = new OpenEquineGenetics.Pair("E", "e");
        var sire = new OpenEquineGenetics.Pair("E", "e");
        System.out.println();
        System.out.println("Foals of an " + dam + " mare and an " + sire + " stallion at extension:");
        for (var foal : extension.cross(dam, sire)) {
            System.out.printf("  %-5s %3.0f%%  %s%n", foal.alleles(), foal.probability() * 100,
                    foal.outcome().phenotype().name());
        }

        // A random Arabian mare, with what shows on her coat.
        var mare = db.randomHorse("arabian", OpenEquineGenetics.MARE);
        var outcomes = db.outcomes(mare.genotype());
        System.out.println();
        System.out.println("A random Arabian mare" + (mare.strain() != null ? " (" + mare.strain() + ")" : "")
                + ", what shows on her coat:");
        for (var gene : db.genes().values()) {
            var o = outcomes.get(gene.id());
            if (gene.affectsCoat() && o != null && o.phenotype() != null && o.phenotype().changesCoat()) {
                System.out.println("  " + gene.name() + " " + mare.genotype().get(gene.id()) + ": " + o.phenotype().name());
            }
        }
    }
}
