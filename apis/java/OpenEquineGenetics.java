/*
 * Open Equine Genetics: a small reader for the genes/ and breeds/ JSON files.
 *
 * One file, Java 17 or newer, no dependencies. Copy it into your project (add a
 * package line if you use packages).
 *
 *     var db = OpenEquineGenetics.Database.load(Path.of("path/to/open-equine-genetics"));
 *     db.gene("extension").outcome("E", "e").phenotype().name();    // "Wild type"
 *
 * The API is the same in every language in apis/; apis/README.md describes it.
 * Where the Python reader gives None, this one gives null.
 */

import java.io.IOException;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Random;
import java.util.TreeMap;
import java.util.function.DoubleSupplier;
import java.util.stream.Stream;

public final class OpenEquineGenetics {
    private OpenEquineGenetics() {}

    public static final String MARE = "mare";
    public static final String STALLION = "stallion";
    public static final String GENERAL_POPULATION = "general population";

    /** Two alleles. Order within a pair carries no meaning; sameAs ignores it. */
    public record Pair(String a, String b) {
        public boolean sameAs(Pair o) {
            if (o == null) return false;
            return (a.equals(o.a) && b.equals(o.b)) || (a.equals(o.b) && b.equals(o.a));
        }

        public boolean contains(String allele) {
            return a.equals(allele) || b.equals(allele);
        }

        @Override
        public String toString() {
            return a + "/" + b;
        }
    }

    /** quality is "verified", "unverified", "abstracted" or "invalid", null on a placeholder.
     *  placeholder is set only on the stand-in "Y" of an x-linked locus. */
    public record Allele(String id, String name, boolean wildType, String quality, String placeholder) {}

    public record Phenotype(String id, String name, String description, boolean changesCoat, boolean documented) {}

    /** severity: "informational", "impairing", "lethal_in_early_life", "lethal_at_birth", "lethal_at_conception". */
    public record Condition(String id, String name, String description, String severity) {}

    public record GenotypeShare(Pair alleles, double percent) {}

    /** A frequency table (in a gene file) or a gene pool (in a breed file).
     *  population is null on a breed pool; evidence is "published" or "estimated". */
    public record GenotypeTable(String locus, String population, String evidence, List<GenotypeShare> genotypes) {}

    public record Citation(String source, String label, String url, boolean verified) {}

    /** What one allele pair at one locus does to a horse. alleles are as the gene file writes
     *  the pair; phenotype is null only for a nonviable pair with no phenotype given. */
    public record Outcome(Pair alleles, Phenotype phenotype, List<Condition> conditions, boolean viable) {}

    /** probability is 0 to 1; outcome is null only if the pair is not in the gene file. */
    public record Offspring(Pair alleles, double probability, Outcome outcome) {}

    public record Strain(String name, double share, List<GenotypeTable> pools) {}

    /** genotype maps every locus id in the database to its pair, in id order. */
    public record Horse(String sex, String breed, String strain, Map<String, Pair> genotype) {}

    /** evidence is "published" or "abstracted" (an abstracted locus is not a real gene);
     *  inheritance is "autosomal", "x_linked" or "y_linked"; raw is the whole file. */
    public record Gene(String id, String name, String symbol, String chromosome, String evidence,
                       String summary, String inheritance, boolean affectsCoat, boolean affectsEye, List<Allele> alleles,
                       List<Phenotype> phenotypes, List<Condition> conditions,
                       List<GenotypeTable> frequencies, List<Citation> citations, Map<String, Object> raw) {

        public Allele allele(String alleleId) {
            return alleles.stream().filter(a -> a.id().equals(alleleId)).findFirst().orElse(null);
        }

        public Phenotype phenotype(String phenotypeId) {
            return phenotypes.stream().filter(p -> p.id().equals(phenotypeId)).findFirst().orElse(null);
        }

        public Condition condition(String conditionId) {
            return conditions.stream().filter(c -> c.id().equals(conditionId)).findFirst().orElse(null);
        }

        public Allele wildType() {
            return alleles.stream().filter(Allele::wildType).findFirst().orElseThrow();
        }

        public Allele placeholder() {
            return alleles.stream().filter(a -> a.placeholder() != null && !a.placeholder().isEmpty())
                    .findFirst().orElse(null);
        }

        /** The outcome of the pair a/b, or null if it is not a pair of this locus. */
        public Outcome outcome(String a, String b) {
            return outcome(a, b, null);
        }

        /** context maps other locus ids to their pairs. Where this locus depends on others
         *  (its "interactions") and context gives all of them, the interaction table decides
         *  the phenotype; otherwise the result is the outcome with the other loci at wild type. */
        public Outcome outcome(String a, String b, Map<String, Pair> context) {
            Pair p = new Pair(a, b);
            for (Object row : list(raw.get("genotypes"))) {
                if (pairOf(obj(row).get("alleles")).sameAs(p)) return outcome(obj(row), true, context);
            }
            for (Object row : list(raw.get("nonviable_genotypes"))) {
                if (pairOf(obj(row).get("alleles")).sameAs(p)) return outcome(obj(row), false, context);
            }
            return null;
        }

        private Outcome outcome(Map<String, Object> row, boolean viable, Map<String, Pair> context) {
            Pair alleles = pairOf(row.get("alleles"));
            String phenotypeId = (String) row.get("phenotype");
            Map<String, Object> interactions = obj(raw.get("interactions"));
            if (viable && !interactions.isEmpty() && context != null && !context.isEmpty()
                    && list(interactions.get("depends_on")).stream().allMatch(context::containsKey)) {
                for (Object e : list(interactions.get("table"))) {
                    Map<String, Object> entry = obj(e);
                    if (!pairOf(entry.get("alleles")).sameAs(alleles)) continue;
                    boolean all = obj(entry.get("with")).entrySet().stream()
                            .allMatch(w -> pairOf(w.getValue()).sameAs(context.get(w.getKey())));
                    if (all) {
                        phenotypeId = (String) entry.get("phenotype");
                        break;
                    }
                }
            }
            List<Condition> conds = new ArrayList<>();
            for (Object c : list(row.get("conditions"))) conds.add(condition((String) c));
            return new Outcome(alleles, phenotypeId != null ? phenotype(phenotypeId) : null,
                    Collections.unmodifiableList(conds), viable);
        }

        /** Every pair a foal of this dam and sire can get at this locus, with its chance.
         *  Each parent passes one of its two alleles, each with chance 1/2. On an x-linked
         *  locus the sire's placeholder "Y" stands for passing his Y chromosome: those foals
         *  are colts. Nonviable pairs are included, marked by outcome.viable. */
        public List<Offspring> cross(Pair dam, Pair sire) {
            List<Offspring> result = new ArrayList<>();
            for (String x : List.of(dam.a(), dam.b())) {
                for (String y : List.of(sire.a(), sire.b())) {
                    Pair p = new Pair(x, y);
                    int i = 0;
                    while (i < result.size() && !result.get(i).alleles().sameAs(p)) i++;
                    if (i < result.size()) {
                        Offspring o = result.get(i);
                        result.set(i, new Offspring(o.alleles(), o.probability() + 0.25, o.outcome()));
                        continue;
                    }
                    Outcome outcome = outcome(x, y);
                    result.add(new Offspring(outcome != null ? outcome.alleles() : p, 0.25, outcome));
                }
            }
            return result;
        }

        public GenotypeTable frequencyTable() {
            return frequencyTable(GENERAL_POPULATION);
        }

        public GenotypeTable frequencyTable(String population) {
            return frequencies.stream().filter(t -> population.equals(t.population())).findFirst().orElse(null);
        }
    }

    /** scores are estimated 1-10 scores, not measurements (evidence always "abstracted"); raw is the whole file. */
    public record Breed(String id, String name, String country, String description, Map<String, Object> scores,
                        List<GenotypeTable> pools, List<Strain> strains, List<Citation> citations,
                        Map<String, Object> raw) {

        public Strain strain(String name) {
            return strains.stream().filter(s -> s.name().equals(name)).findFirst().orElse(null);
        }

        public GenotypeTable pool(String locus) {
            return pool(locus, null);
        }

        /** The breed's gene pool at a locus: the strain's if it has one there, else the breed's. */
        public GenotypeTable pool(String locus, String strain) {
            Strain s = strain != null ? strain(strain) : null;
            if (s != null) {
                for (GenotypeTable p : s.pools()) if (p.locus().equals(locus)) return p;
            }
            for (GenotypeTable p : pools) if (p.locus().equals(locus)) return p;
            return null;
        }
    }

    /** Draw one pair from a table, or null if its percents total 0. rng returns a number in [0, 1). */
    public static Pair sample(GenotypeTable table, DoubleSupplier rng) {
        double total = 0;
        for (GenotypeShare g : table.genotypes()) total += g.percent();
        if (total <= 0) return null;
        double target = rng.getAsDouble() * total;
        double running = 0.0;
        for (GenotypeShare g : table.genotypes()) {
            running += g.percent();
            if (target < running) return g.alleles();
        }
        return table.genotypes().get(table.genotypes().size() - 1).alleles();
    }

    public static Pair sample(GenotypeTable table) {
        return sample(table, new Random()::nextDouble);
    }

    public static final class Database {
        private final Map<String, Gene> genes;
        private final Map<String, Breed> breeds;

        public Database(Map<String, Gene> genes, Map<String, Breed> breeds) {
            this.genes = Collections.unmodifiableMap(new TreeMap<>(genes));
            this.breeds = Collections.unmodifiableMap(new TreeMap<>(breeds));
        }

        /** id -> Gene, sorted by id. */
        public Map<String, Gene> genes() {
            return genes;
        }

        /** id -> Breed, sorted by id. */
        public Map<String, Breed> breeds() {
            return breeds;
        }

        /** Read every file in root/genes and root/breeds. root is the repository folder. */
        public static Database load(Path root) {
            Map<String, Gene> genes = new TreeMap<>();
            for (Map<String, Object> d : readDir(root.resolve("genes"))) {
                Gene g = parseGene(d);
                genes.put(g.id(), g);
            }
            Map<String, Breed> breeds = new TreeMap<>();
            for (Map<String, Object> d : readDir(root.resolve("breeds"))) {
                Breed b = parseBreed(d);
                breeds.put(b.id(), b);
            }
            return new Database(genes, breeds);
        }

        public Gene gene(String geneId) {
            return genes.get(geneId);
        }

        public Breed breed(String breedId) {
            return breeds.get(breedId);
        }

        /** Where a horse's pair at a locus is drawn from: the breed (or strain) pool,
         *  else the gene's general-population table, else null. breed and strain may be null. */
        public GenotypeTable genotypeTable(String locus, String breed, String strain) {
            Breed b = breed != null ? breeds.get(breed) : null;
            GenotypeTable table = b != null ? b.pool(locus, strain) : null;
            if (table == null && genes.containsKey(locus)) table = genes.get(locus).frequencyTable();
            return table;
        }

        public Horse randomHorse(String breed) {
            return randomHorse(breed, MARE, null, new Random()::nextDouble);
        }

        public Horse randomHorse(String breed, String sex) {
            return randomHorse(breed, sex, null, new Random()::nextDouble);
        }

        public Horse randomHorse(String breed, String sex, String strain) {
            return randomHorse(breed, sex, strain, new Random()::nextDouble);
        }

        /** A random horse: a pair at every locus. With no breed (null), the general population.
         *  If the breed has strains and none is named, one is drawn by share. A locus with
         *  no table gets the wild-type pair. */
        public Horse randomHorse(String breed, String sex, String strain, DoubleSupplier rng) {
            Breed b = breed != null ? breeds.get(breed) : null;
            if (b != null && !b.strains().isEmpty() && strain == null) {
                List<GenotypeShare> rows = new ArrayList<>();
                for (Strain s : b.strains()) rows.add(new GenotypeShare(new Pair(s.name(), s.name()), s.share()));
                Pair drawn = sample(new GenotypeTable("", null, "estimated", rows), rng);
                strain = drawn != null ? drawn.a() : null;
            }
            Map<String, Pair> genotype = new LinkedHashMap<>();
            for (Gene gene : genes.values()) {
                genotype.put(gene.id(), draw(gene, sex, genotypeTable(gene.id(), breed, strain), rng));
            }
            return new Horse(sex, breed, strain, Collections.unmodifiableMap(genotype));
        }

        private static Pair draw(Gene gene, String sex, GenotypeTable table, DoubleSupplier rng) {
            Allele placeholder = gene.placeholder();
            boolean male = STALLION.equals(sex);
            if (table != null && placeholder != null) {
                List<GenotypeShare> rows = new ArrayList<>();
                for (GenotypeShare g : table.genotypes()) {
                    if (g.alleles().contains(placeholder.id()) == male) rows.add(g);
                }
                table = new GenotypeTable(table.locus(), table.population(), table.evidence(), rows);
            }
            Pair pair = table != null ? sample(table, rng) : null;
            if (pair != null) return pair;
            String wild = gene.wildType().id();
            return placeholder != null && male ? new Pair(wild, placeholder.id()) : new Pair(wild, wild);
        }

        /** The outcome at every locus in a genotype, with interactions between loci applied. */
        public Map<String, Outcome> outcomes(Map<String, Pair> genotype) {
            Map<String, Outcome> out = new LinkedHashMap<>();
            for (Map.Entry<String, Pair> e : genotype.entrySet()) {
                Gene g = genes.get(e.getKey());
                if (g != null) out.put(e.getKey(), g.outcome(e.getValue().a(), e.getValue().b(), genotype));
            }
            return out;
        }
    }

    // Reading the files.

    private static List<Map<String, Object>> readDir(Path dir) {
        try (Stream<Path> files = Files.list(dir)) {
            List<Path> paths = files.filter(p -> p.getFileName().toString().endsWith(".json")).sorted().toList();
            List<Map<String, Object>> out = new ArrayList<>();
            for (Path p : paths) out.add(obj(new Json(Files.readString(p, StandardCharsets.UTF_8)).document()));
            return out;
        } catch (IOException e) {
            throw new UncheckedIOException(e);
        }
    }

    private static GenotypeTable table(Map<String, Object> d, String locus, String population) {
        List<GenotypeShare> rows = new ArrayList<>();
        for (Object g : list(d.get("genotypes"))) {
            rows.add(new GenotypeShare(pairOf(obj(g).get("alleles")), num(obj(g).get("percent"))));
        }
        return new GenotypeTable(locus, population, (String) d.get("evidence"), Collections.unmodifiableList(rows));
    }

    private static List<Citation> citations(Map<String, Object> d) {
        List<Citation> out = new ArrayList<>();
        for (Object o : list(d.get("citations"))) {
            Map<String, Object> c = obj(o);
            out.add(new Citation((String) c.get("source"), (String) c.get("label"), (String) c.get("url"),
                    bool(c.get("verified"), false)));
        }
        return Collections.unmodifiableList(out);
    }

    private static Gene parseGene(Map<String, Object> d) {
        String id = (String) d.get("id");
        List<Allele> alleles = new ArrayList<>();
        for (Object o : list(d.get("alleles"))) {
            Map<String, Object> a = obj(o);
            alleles.add(new Allele((String) a.get("id"), (String) a.get("name"), bool(a.get("wild_type"), false),
                    (String) a.get("quality"), (String) a.get("placeholder")));
        }
        List<Phenotype> phenotypes = new ArrayList<>();
        for (Object o : list(d.get("phenotypes"))) {
            Map<String, Object> p = obj(o);
            phenotypes.add(new Phenotype((String) p.get("id"), (String) p.get("name"), (String) p.get("description"),
                    bool(p.get("changes_coat"), false), bool(p.get("documented"), true)));
        }
        List<Condition> conditions = new ArrayList<>();
        for (Object o : list(d.get("conditions"))) {
            Map<String, Object> c = obj(o);
            conditions.add(new Condition((String) c.get("id"), (String) c.get("name"), (String) c.get("description"),
                    (String) c.get("severity")));
        }
        List<GenotypeTable> frequencies = new ArrayList<>();
        for (Object t : list(d.get("frequencies"))) {
            frequencies.add(table(obj(t), id, (String) obj(t).get("population")));
        }
        return new Gene(id, (String) d.get("name"), (String) d.get("symbol"), (String) d.get("chromosome"),
                (String) d.get("evidence"), (String) d.get("summary"), (String) d.get("inheritance"),
                bool(d.get("affects_coat"), false), bool(d.get("affects_eye"), false), Collections.unmodifiableList(alleles),
                Collections.unmodifiableList(phenotypes), Collections.unmodifiableList(conditions),
                Collections.unmodifiableList(frequencies), citations(d), d);
    }

    private static Breed parseBreed(Map<String, Object> d) {
        List<GenotypeTable> pools = new ArrayList<>();
        for (Object p : list(d.get("pools"))) pools.add(table(obj(p), (String) obj(p).get("locus"), null));
        List<Strain> strains = new ArrayList<>();
        for (Object o : list(d.get("strains"))) {
            Map<String, Object> s = obj(o);
            List<GenotypeTable> sp = new ArrayList<>();
            for (Object p : list(s.get("pools"))) sp.add(table(obj(p), (String) obj(p).get("locus"), null));
            strains.add(new Strain((String) s.get("name"), num(s.get("share")), Collections.unmodifiableList(sp)));
        }
        return new Breed((String) d.get("id"), (String) d.get("name"), (String) d.get("country"),
                (String) d.get("description"), obj(d.get("scores")), Collections.unmodifiableList(pools),
                Collections.unmodifiableList(strains), citations(d), d);
    }

    @SuppressWarnings("unchecked")
    private static Map<String, Object> obj(Object o) {
        return o instanceof Map ? (Map<String, Object>) o : Map.of();
    }

    @SuppressWarnings("unchecked")
    private static List<Object> list(Object o) {
        return o instanceof List ? (List<Object>) o : List.of();
    }

    private static Pair pairOf(Object o) {
        List<Object> l = list(o);
        return new Pair((String) l.get(0), (String) l.get(1));
    }

    private static double num(Object o) {
        return o instanceof Number n ? n.doubleValue() : 0;
    }

    private static boolean bool(Object o, boolean fallback) {
        return o instanceof Boolean b ? b : fallback;
    }

    /** A small JSON parser. Objects keep their key order; numbers are Doubles. */
    private static final class Json {
        private final String s;
        private int i;

        Json(String s) {
            this.s = s;
            if (s.startsWith("﻿")) i = 1;
        }

        Object document() {
            Object v = value();
            space();
            if (i != s.length()) throw error("trailing text");
            return v;
        }

        private Object value() {
            space();
            if (i >= s.length()) throw error("unexpected end");
            char c = s.charAt(i);
            switch (c) {
                case '{': return object();
                case '[': return array();
                case '"': return string();
                case 't': return word("true", Boolean.TRUE);
                case 'f': return word("false", Boolean.FALSE);
                case 'n': return word("null", null);
                default: return number();
            }
        }

        private Map<String, Object> object() {
            Map<String, Object> m = new LinkedHashMap<>();
            i++;
            space();
            if (peek() == '}') { i++; return m; }
            while (true) {
                space();
                if (peek() != '"') throw error("expected a key");
                String k = string();
                space();
                expect(':');
                m.put(k, value());
                space();
                if (peek() == ',') { i++; continue; }
                expect('}');
                return m;
            }
        }

        private List<Object> array() {
            List<Object> l = new ArrayList<>();
            i++;
            space();
            if (peek() == ']') { i++; return l; }
            while (true) {
                l.add(value());
                space();
                if (peek() == ',') { i++; continue; }
                expect(']');
                return l;
            }
        }

        private String string() {
            i++;
            StringBuilder b = new StringBuilder();
            while (true) {
                if (i >= s.length()) throw error("unterminated string");
                char c = s.charAt(i++);
                if (c == '"') return b.toString();
                if (c != '\\') { b.append(c); continue; }
                char e = s.charAt(i++);
                switch (e) {
                    case '"', '\\', '/' -> b.append(e);
                    case 'b' -> b.append('\b');
                    case 'f' -> b.append('\f');
                    case 'n' -> b.append('\n');
                    case 'r' -> b.append('\r');
                    case 't' -> b.append('\t');
                    case 'u' -> { b.append((char) Integer.parseInt(s.substring(i, i + 4), 16)); i += 4; }
                    default -> throw error("bad escape");
                }
            }
        }

        private Double number() {
            int start = i;
            while (i < s.length() && "+-0123456789.eE".indexOf(s.charAt(i)) >= 0) i++;
            if (start == i) throw error("unexpected character");
            return Double.parseDouble(s.substring(start, i));
        }

        private Object word(String w, Object v) {
            if (!s.startsWith(w, i)) throw error("unexpected word");
            i += w.length();
            return v;
        }

        private void space() {
            while (i < s.length() && Character.isWhitespace(s.charAt(i))) i++;
        }

        private char peek() {
            return i < s.length() ? s.charAt(i) : '\0';
        }

        private void expect(char c) {
            if (peek() != c) throw error("expected '" + c + "'");
            i++;
        }

        private IllegalArgumentException error(String what) {
            return new IllegalArgumentException("JSON: " + what + " at offset " + i);
        }
    }
}
