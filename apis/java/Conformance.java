/*
 * Prints the conformance report described in apis/README.md. Every port prints the same.
 *
 *     java Conformance.java ../..                  (Java 22 or newer)
 *     javac -d out *.java && java -cp out Conformance ../..
 */

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;

public class Conformance {
    static String phen(OpenEquineGenetics.Outcome o) {
        return o != null && o.phenotype() != null ? o.phenotype().id() : "-";
    }

    @SuppressWarnings("unchecked")
    static List<Object> list(Object o) {
        return o instanceof List ? (List<Object>) o : List.of();
    }

    @SuppressWarnings("unchecked")
    static Map<String, Object> obj(Object o) {
        return o instanceof Map ? (Map<String, Object>) o : Map.of();
    }

    static OpenEquineGenetics.Pair pair(Object jsonPair) {
        List<Object> l = list(jsonPair);
        return new OpenEquineGenetics.Pair((String) l.get(0), (String) l.get(1));
    }

    public static void main(String[] args) throws IOException {
        var db = OpenEquineGenetics.Database.load(Path.of(args.length > 0 ? args[0] : "../.."));
        StringBuilder out = new StringBuilder();
        for (var gene : db.genes().values()) {
            List<Object> rows = list(gene.raw().get("genotypes"));
            List<Object> all = new ArrayList<>(rows);
            all.addAll(list(gene.raw().get("nonviable_genotypes")));
            out.append("gene ").append(gene.id()).append(' ').append(gene.inheritance()).append(' ')
                    .append(gene.evidence()).append(" alleles=").append(gene.alleles().size()).append('\n');
            for (Object row : all) {
                var p = pair(obj(row).get("alleles"));
                var o = gene.outcome(p.b(), p.a());
                List<String> ids = o.conditions().stream().map(OpenEquineGenetics.Condition::id).toList();
                out.append("  outcome ").append(o.alleles()).append(" -> ").append(phen(o))
                        .append(" [").append(String.join(",", ids)).append("] viable=").append(o.viable()).append('\n');
            }
            for (Object e : list(obj(gene.raw().get("interactions")).get("table"))) {
                Map<String, OpenEquineGenetics.Pair> context = new LinkedHashMap<>();
                obj(obj(e).get("with")).forEach((locus, p) -> context.put(locus, pair(p)));
                var p = pair(obj(e).get("alleles"));
                var o = gene.outcome(p.a(), p.b(), context);
                List<String> with = new ArrayList<>();
                context.forEach((locus, cp) -> with.add(locus + "=" + cp));
                out.append("  with ").append(String.join(",", with)).append(" : ").append(o.alleles())
                        .append(" -> ").append(phen(o)).append('\n');
            }
            var placeholder = gene.placeholder();
            // The dam is the first mare's pair, the sire the last stallion's, heterozygous where possible.
            List<OpenEquineGenetics.Pair> mares = new ArrayList<>(), sires = new ArrayList<>();
            for (Object row : rows) {
                var p = pair(obj(row).get("alleles"));
                if (placeholder == null || !p.contains(placeholder.id())) mares.add(p);
                if (placeholder == null || p.contains(placeholder.id())) sires.add(p);
            }
            var hetMares = mares.stream().filter(p -> !p.a().equals(p.b())).toList();
            var hetSires = sires.stream().filter(p -> !p.a().equals(p.b())).toList();
            var dam = (hetMares.isEmpty() ? mares : hetMares).get(0);
            var sireList = hetSires.isEmpty() ? sires : hetSires;
            var sire = sireList.get(sireList.size() - 1);
            out.append("  cross ").append(dam).append(" x ").append(sire).append('\n');
            for (var child : gene.cross(dam, sire)) {
                out.append("    ").append(child.alleles()).append(' ')
                        .append(String.format(Locale.ROOT, "%.6f", child.probability())).append(' ')
                        .append(phen(child.outcome())).append('\n');
            }
        }
        int i = 0;
        for (var breed : db.breeds().values()) {
            out.append("breed ").append(breed.id()).append(" strains=").append(breed.strains().size()).append('\n');
            List<String> sexes = i < 3 ? List.of(OpenEquineGenetics.MARE, OpenEquineGenetics.STALLION)
                                       : List.of(OpenEquineGenetics.MARE);
            for (String sex : sexes) {
                var horse = db.randomHorse(breed.id(), sex, null, () -> 0.5);
                out.append("  horse ").append(horse.sex()).append(" strain=")
                        .append(horse.strain() != null ? horse.strain() : "-").append('\n');
                var outcomes = db.outcomes(horse.genotype());
                horse.genotype().forEach((locus, p) ->
                        out.append("    ").append(locus).append(' ').append(p).append(' ')
                                .append(phen(outcomes.get(locus))).append('\n'));
            }
            i++;
        }
        System.out.write(out.toString().getBytes(StandardCharsets.UTF_8));
        System.out.flush();
    }
}
