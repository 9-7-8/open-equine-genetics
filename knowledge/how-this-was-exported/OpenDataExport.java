package com.example.horsegenetics.common.genetics;

import com.example.horsegenetics.common.trait.Condition;
import com.example.horsegenetics.common.trait.ExportTraitAccess;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Scratch exporter: the natural and abstracted genes as language-agnostic JSON. Not part of the mod. */
public final class OpenDataExport {

    static final String PREFIX = "horsegenetics.";

    /** Natural-flagged stand-ins for something real that is not understood (owner, 2026-10-12). */
    static final Set<String> ABSTRACTED_NATURAL = Set.of("flaxen", "shade", "sooty", "markings", "pangare",
            "rabicano", "manchado", "cvm", "megaesophagus", "patn2", "fertility", "natural_zebra",
            "eye_colour_right", "eye_colour_left", "eye_sector_right", "eye_sector_left",
            "eye_sector_colour_right", "eye_sector_colour_left", "eye_sclera_right", "eye_sclera_left");

    /** Natural-flagged, staying in the mod. */
    static final Set<String> EXCLUDED = Set.of("diet", "sex");

    /** The five body-stat genes: mod key suffix -> { repo id, name }. */
    static final Map<String, String[]> STAT = new LinkedHashMap<>();
    static {
        STAT.put("body_size", new String[] {"abstracted_height", "Abstracted height",
                "A stand-in for the many loci that set a horse's height. One allele raises height, one lowers it, and the two copies add. Real height is polygenic; this single locus is an abstraction, not a claim about a real gene."});
        STAT.put("magic_speed", new String[] {"abstracted_speed", "Abstracted speed",
                "A stand-in for the many loci that set a horse's speed. One allele raises it, one lowers it, and the two copies add. The real trait is polygenic; this single locus is an abstraction, not a claim about a real gene."});
        STAT.put("magic_jump", new String[] {"abstracted_jump", "Abstracted jump",
                "A stand-in for the many loci that set a horse's jumping ability. One allele raises it, one lowers it, and the two copies add. The real trait is polygenic; this single locus is an abstraction, not a claim about a real gene."});
        STAT.put("magic_health", new String[] {"abstracted_hardiness", "Abstracted hardiness",
                "A stand-in for the many loci that set a horse's hardiness. One allele raises it, one lowers it, and the two copies add. The real trait is polygenic; this single locus is an abstraction, not a claim about a real gene."});
        STAT.put("magic_pull", new String[] {"abstracted_draft_power", "Abstracted draft power",
                "A stand-in for the many loci that set a horse's pulling power. One allele raises it, one lowers it, and the two copies add. The real trait is polygenic; this single locus is an abstraction, not a claim about a real gene."});
    }

    /** Outcome depends on these other loci. */
    static final Map<String, List<String>> DEPENDS = Map.of(
            "agouti", List.of("extension", "shade"),
            "leopard", List.of("patn1", "patn2"),
            "flaxen", List.of("extension"));

    public static void main(String[] args) throws Exception {
        Path out = Path.of(args[0]);
        Files.createDirectories(out.resolve("genes"));
        StringBuilder map = new StringBuilder("{\n  \"format\": 1,\n  \"consumer\": \"Procedural-Horse-Coats\",\n"
                + "  \"note\": \"Repo locus and allele ids to the mod's saved gene keys and allele tokens."
                + " The allele order is the order the mod requires and must never change.\",\n  \"loci\": [\n");
        boolean firstMap = true;
        int n = 0;
        for (Gene gene : Genes.all()) {
            String shortKey = gene.key().startsWith(PREFIX) ? gene.key().substring(PREFIX.length()) : null;
            if (shortKey == null) continue;
            boolean stat = STAT.containsKey(shortKey);
            if (!stat && (!gene.isNatural() || EXCLUDED.contains(shortKey))) continue;
            String id = stat ? STAT.get(shortKey)[0] : shortKey;
            String name = stat ? STAT.get(shortKey)[1] : gene.name();
            String evidence = stat || ABSTRACTED_NATURAL.contains(shortKey) ? "abstracted" : "published";
            Files.writeString(out.resolve("genes").resolve(id + ".json"),
                    locus(gene, id, name, evidence, stat), StandardCharsets.UTF_8);
            if (!firstMap) map.append(",\n");
            firstMap = false;
            map.append("    { \"locus\": ").append(q(id)).append(", \"mod_key\": ").append(q(gene.key()))
                    .append(", \"mod_allele_order\": ").append(tokens(gene.alleles(), false));
            if (stat) map.append(", \"mod_only_alleles\": [\"Vmp\"]");
            map.append(" }");
            n++;
        }
        map.append("\n  ]\n}\n");
        Files.writeString(out.resolve("mod-map.json"), map.toString(), StandardCharsets.UTF_8);
        System.out.println("exported " + n + " loci");
    }

    static boolean kept(Allele a, boolean stat) {
        return !(stat && a.token().equals("Vmp"));
    }

    static boolean kept(AllelePair p, boolean stat) {
        return kept(p.first(), stat) && kept(p.second(), stat);
    }

    static String tokens(List<Allele> alleles, boolean stat) {
        List<String> t = new ArrayList<>();
        for (Allele a : alleles) if (kept(a, stat)) t.add(q(a.token()));
        return "[" + String.join(", ", t) + "]";
    }

    static String pair(AllelePair p) {
        return "[" + q(p.first().token()) + ", " + q(p.second().token()) + "]";
    }

    static String locus(Gene gene, String id, String name, String evidence, boolean stat) {
        StringBuilder s = new StringBuilder();
        s.append("{\n  \"format\": 1,\n");
        s.append("  \"id\": ").append(q(id)).append(",\n");
        s.append("  \"species\": \"horse\",\n");
        s.append("  \"name\": ").append(q(name)).append(",\n");
        s.append("  \"symbol\": null,\n");
        s.append("  \"evidence\": ").append(q(evidence)).append(",\n");
        s.append("  \"summary\": ").append(q(stat ? STAT.get(gene.key().substring(PREFIX.length()))[2] : gene.description())).append(",\n");
        String inh = gene.inheritance().name().toLowerCase();
        s.append("  \"inheritance\": ").append(q(inh)).append(",\n");
        s.append("  \"affects_coat\": ").append(gene.affectsCoat()).append(",\n");

        // alleles
        Allele placeholder = gene.inheritance().name().equals("AUTOSOMAL") ? null : gene.hemizygousPlaceholder();
        s.append("  \"alleles\": [\n");
        List<String> rows = new ArrayList<>();
        for (Allele a : gene.alleles()) {
            if (!kept(a, stat)) continue;
            String row = "    { \"id\": " + q(a.token()) + ", \"name\": " + q(a.label())
                    + ", \"wild_type\": " + a.equals(gene.defaultAllele());
            if (a.equals(placeholder)) row += ", \"placeholder\": \"the missing second copy in the hemizygous sex\"";
            rows.add(row + " }");
        }
        s.append(String.join(",\n", rows)).append("\n  ],\n");

        // genotypes, and what is used
        List<AllelePair> pairs = new ArrayList<>();
        for (AllelePair p : GenotypeCatalog.allPairsOf(gene)) if (kept(p, stat)) pairs.add(p);
        Set<Expression> used = new LinkedHashSet<>(stat ? List.<Expression>of() : gene.expressions());
        Map<String, Condition> conditions = new LinkedHashMap<>();
        List<String> geno = new ArrayList<>();
        for (AllelePair p : pairs) {
            Expression e = gene.expressionOf(p);
            used.add(e);
            List<String> cids = new ArrayList<>();
            for (Condition c : ExportTraitAccess.conditions(gene, p)) {
                conditions.putIfAbsent(c.id(), c);
                cids.add(q(c.id()));
            }
            String row = "    { \"alleles\": " + pair(p) + ", \"phenotype\": " + q(e.id());
            if (!cids.isEmpty()) row += ", \"conditions\": [" + String.join(", ", cids) + "]";
            geno.add(row + " }");
        }

        // interactions
        List<String> inter = new ArrayList<>();
        List<String> deps = DEPENDS.get(id);
        if (deps != null) {
            List<Gene> others = new ArrayList<>();
            for (String d : deps) others.add(Genes.byKey(PREFIX + d));
            List<List<AllelePair>> combos = new ArrayList<>();
            combos.add(new ArrayList<>());
            for (Gene o : others) {
                List<List<AllelePair>> next = new ArrayList<>();
                for (List<AllelePair> c : combos) {
                    for (AllelePair op : GenotypeCatalog.allPairsOf(o)) {
                        List<AllelePair> c2 = new ArrayList<>(c);
                        c2.add(op);
                        next.add(c2);
                    }
                }
                combos = next;
            }
            for (AllelePair p : pairs) {
                for (List<AllelePair> c : combos) {
                    Genotype g = Genotype.wildType().with(p).withAll(c);
                    Expression e = gene.expressionIn(p, g);
                    used.add(e);
                    List<String> with = new ArrayList<>();
                    for (int i = 0; i < deps.size(); i++) with.add(q(deps.get(i)) + ": " + pair(c.get(i)));
                    inter.add("      { \"alleles\": " + pair(p) + ", \"with\": { " + String.join(", ", with)
                            + " }, \"phenotype\": " + q(e.id()) + " }");
                }
            }
        }

        // nonviable genotypes (computed before the phenotype list, so their phenotype counts as used)
        List<String> nonviable = new ArrayList<>();
        List<Allele> al = gene.alleles();
        for (int i = 0; i < al.size(); i++) {
            for (int j = i; j < al.size(); j++) {
                AllelePair p = new AllelePair(al.get(i), al.get(j));
                if (!kept(p, stat) || !gene.sexConsistent(p) || gene.canOccur(p)) continue;
                String ph = "";
                try {
                    Expression e = gene.expressionOf(p);
                    used.add(e);
                    ph = ", \"phenotype\": " + q(e.id());
                } catch (RuntimeException ignored) { }
                nonviable.add("    { \"alleles\": " + pair(p) + ph + " }");
            }
        }

        s.append("  \"phenotypes\": [\n");
        rows = new ArrayList<>();
        for (Expression e : used) {
            rows.add("    { \"id\": " + q(e.id()) + ", \"name\": " + q(e.name()) + ", \"description\": "
                    + q(e.description()) + ", \"changes_coat\": " + !e.wildType() + " }");
        }
        s.append(String.join(",\n", rows)).append("\n  ],\n");

        s.append("  \"genotypes\": [\n").append(String.join(",\n", geno)).append("\n  ],\n");

        if (!nonviable.isEmpty()) {
            s.append("  \"nonviable_genotypes\": [\n").append(String.join(",\n", nonviable)).append("\n  ],\n");
        }

        if (!conditions.isEmpty()) {
            rows = new ArrayList<>();
            for (Condition c : conditions.values()) {
                rows.add("    { \"id\": " + q(c.id()) + ", \"name\": " + q(c.name()) + ", \"description\": "
                        + q(c.description()) + ", \"severity\": " + q(c.severity().name().toLowerCase()) + " }");
            }
            s.append("  \"conditions\": [\n").append(String.join(",\n", rows)).append("\n  ],\n");
        }

        if (!inter.isEmpty()) {
            s.append("  \"interactions\": {\n    \"depends_on\": [");
            List<String> d = new ArrayList<>();
            for (String x : deps) d.add(q(x));
            s.append(String.join(", ", d)).append("],\n    \"note\": \"The phenotype of this locus once the loci it"
                    + " depends on are known. Overrides the genotypes table.\",\n    \"table\": [\n");
            s.append(String.join(",\n", inter)).append("\n    ]\n  },\n");
        }

        // frequencies
        s.append("  \"frequencies\": [\n");
        rows = new ArrayList<>();
        rows.add(freq(gene, stat, "general population", new LinkedHashMap<>()));
        if (id.equals("brindle")) {
            rows.clear();
            for (AllelePair sp : GenotypeCatalog.allPairsOf(Genes.SEX)) {
                Map<String, AllelePair> drawn = new LinkedHashMap<>();
                drawn.put(Genes.SEX.key(), sp);
                rows.add(freq(gene, stat, Genes.SEX.expressionOf(sp).name().toLowerCase() + "s", drawn));
            }
        }
        if (id.equals("patn1") || id.equals("patn2")) {
            Gene lp = Genes.byKey(PREFIX + "leopard");
            for (AllelePair lpp : GenotypeCatalog.allPairsOf(lp)) {
                if (!lp.expressionOf(lpp).wildType() && lpp.homozygous()) {
                    Map<String, AllelePair> drawn = new LinkedHashMap<>();
                    drawn.put(lp.key(), lpp);
                    rows.set(0, freq(gene, stat, "horses without the leopard complex allele", new LinkedHashMap<>()));
                    rows.add(freq(gene, stat, "horses carrying the leopard complex allele", drawn));
                    break;
                }
            }
        }
        s.append(String.join(",\n", rows)).append("\n  ],\n");
        s.append("  \"citations\": []\n}\n");
        return s.toString();
    }

    static String freq(Gene gene, boolean stat, String population, Map<String, AllelePair> drawn) {
        FounderTable t = gene.founderTable(new FounderContext(drawn, gene));
        double total = 0;
        for (AllelePair p : t.pairs()) if (kept(p, stat)) total += t.share(p);
        List<String> rows = new ArrayList<>();
        for (AllelePair p : t.pairs()) {
            if (!kept(p, stat)) continue;
            double pct = Math.round(t.share(p) / total * 1000000.0) / 10000.0;
            rows.add("        { \"alleles\": " + pair(p) + ", \"percent\": " + num(pct) + " }");
        }
        return "    {\n      \"population\": " + q(population) + ",\n      \"evidence\": \"estimated\",\n"
                + "      \"genotypes\": [\n" + String.join(",\n", rows) + "\n      ]\n    }";
    }

    static String num(double d) {
        return d == Math.rint(d) ? String.valueOf((long) d) : String.valueOf(d);
    }

    static String q(String v) {
        if (v == null) return "null";
        StringBuilder b = new StringBuilder("\"");
        for (char c : v.toCharArray()) {
            switch (c) {
                case '"' -> b.append("\\\"");
                case '\\' -> b.append("\\\\");
                case '\n' -> b.append("\\n");
                case '\r' -> { }
                case '\t' -> b.append(' ');
                default -> { if (c < 0x20) b.append(' '); else b.append(c); }
            }
        }
        return b.append('"').toString();
    }
}
