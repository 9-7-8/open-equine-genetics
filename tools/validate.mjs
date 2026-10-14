#!/usr/bin/env node
// Checks every genes/*.json against the rules in knowledge/format.txt, and mod-map.json
// against the genes. Needs Node 22 or newer, no packages.
//
//   node tools/validate.mjs
//
// Exit 0 and one summary line when clean; exit 1 and a list of problems when not.

import { readFileSync, readdirSync } from "node:fs";
import { join, dirname } from "node:path";
import { fileURLToPath } from "node:url";

const root = join(dirname(fileURLToPath(import.meta.url)), "..");
const problems = [];
const EVIDENCE = ["published", "abstracted"];
const FREQ_EVIDENCE = ["published", "estimated"];
const INHERITANCE = ["autosomal", "x_linked", "y_linked"];
const SEVERITY = ["informational", "impairing", "lethal_in_early_life", "lethal_at_birth", "lethal_at_conception"];
// The horse has 31 pairs of autosomes.
const CHROMOSOMES = [...Array.from({ length: 31 }, (_, i) => String(i + 1)), "X", "Y"];
const QUALITY = ["verified", "unverified", "abstracted", "invalid"];
const SOURCES = ["ucdavis-vgl", "omia", "pubmed", "paper", "other"];
const key = (pair) => [...pair].sort().join("/");

const genes = new Map();
for (const file of readdirSync(join(root, "genes")).filter((f) => f.endsWith(".json")).sort()) {
  const bad = (msg) => problems.push(`genes/${file}: ${msg}`);
  let g;
  try {
    g = JSON.parse(readFileSync(join(root, "genes", file), "utf8"));
  } catch (e) {
    bad(`does not parse: ${e.message}`);
    continue;
  }
  genes.set(g.id, g);
  if (g.format !== 2) bad(`format is ${g.format}, expected 2`);
  if (`${g.id}.json` !== file) bad(`id "${g.id}" does not match the file name`);
  if (g.species !== "horse") bad(`species is "${g.species}"`);
  if (!EVIDENCE.includes(g.evidence)) bad(`evidence is "${g.evidence}"`);
  if (!INHERITANCE.includes(g.inheritance)) bad(`inheritance is "${g.inheritance}"`);
  if (g.evidence === "abstracted" && g.symbol !== null) bad("an abstracted locus has a gene symbol");
  for (const f of ["name", "summary"]) if (!g[f]) bad(`"${f}" is empty`);
  if (g.chromosome !== undefined) {
    if (!CHROMOSOMES.includes(g.chromosome)) bad(`chromosome is "${g.chromosome}"`);
    if (g.evidence === "abstracted") bad("an abstracted locus has a chromosome");
    const sexLinked = { X: "x_linked", Y: "y_linked" }[g.chromosome] ?? "autosomal";
    if (g.inheritance !== sexLinked) bad(`chromosome "${g.chromosome}" but inheritance "${g.inheritance}"`);
  }

  const ids = (g.alleles ?? []).map((a) => a.id);
  if (new Set(ids).size !== ids.length) bad("duplicate allele id");
  if ((g.alleles ?? []).filter((a) => a.wild_type).length !== 1) bad("not exactly one wild_type allele");
  for (const a of g.alleles ?? []) {
    if (a.placeholder) continue;
    if (!QUALITY.includes(a.quality)) bad(`allele "${a.id}": quality is "${a.quality}"`);
    if (g.evidence === "abstracted" && a.quality !== "abstracted") bad(`allele "${a.id}" of an abstracted locus is not marked abstracted`);
    if (a.wild_type && a.quality === "invalid") bad(`the wild-type allele "${a.id}" is marked invalid`);
  }
  const placeholder = (g.alleles ?? []).find((a) => a.placeholder)?.id;
  if ((g.inheritance !== "autosomal") !== (placeholder !== undefined)) bad("placeholder allele and inheritance disagree");

  // Every pair that can exist, exactly once, across genotypes and nonviable_genotypes.
  const expected = new Set();
  for (let i = 0; i < ids.length; i++) {
    for (let j = i; j < ids.length; j++) {
      // A sex-linked locus: two real alleles (the sex with two copies) or one real + placeholder.
      if (placeholder !== undefined && ids[i] === placeholder && ids[j] === placeholder) continue;
      expected.add(key([ids[i], ids[j]]));
    }
  }
  const seen = new Set();
  const phenotypes = new Set((g.phenotypes ?? []).map((p) => p.id));
  const conditions = new Set((g.conditions ?? []).map((c) => c.id));
  const usedPhenotypes = new Set();
  const usedConditions = new Set();
  const checkPair = (pair, where) => {
    if (!Array.isArray(pair) || pair.length !== 2) return bad(`${where}: alleles is not a pair`);
    for (const a of pair) if (!ids.includes(a)) bad(`${where}: unknown allele "${a}"`);
  };
  for (const row of g.genotypes ?? []) {
    checkPair(row.alleles, "genotypes");
    const k = key(row.alleles);
    if (seen.has(k)) bad(`genotype ${k} listed twice`);
    seen.add(k);
    if (!phenotypes.has(row.phenotype)) bad(`genotype ${k}: unknown phenotype "${row.phenotype}"`);
    usedPhenotypes.add(row.phenotype);
    for (const c of row.conditions ?? []) {
      if (!conditions.has(c)) bad(`genotype ${k}: unknown condition "${c}"`);
      usedConditions.add(c);
    }
  }
  for (const row of g.nonviable_genotypes ?? []) {
    checkPair(row.alleles, "nonviable_genotypes");
    const k = key(row.alleles);
    if (seen.has(k)) bad(`pair ${k} is both a genotype and nonviable`);
    seen.add(k);
    if (row.phenotype !== undefined) {
      if (!phenotypes.has(row.phenotype)) bad(`nonviable ${k}: unknown phenotype "${row.phenotype}"`);
      usedPhenotypes.add(row.phenotype);
    }
  }
  for (const k of expected) if (!seen.has(k)) bad(`pair ${k} is missing`);
  for (const k of seen) if (!expected.has(k)) bad(`pair ${k} cannot exist at this locus`);

  for (const row of g.interactions?.table ?? []) {
    checkPair(row.alleles, "interactions");
    if (!phenotypes.has(row.phenotype)) bad(`interactions: unknown phenotype "${row.phenotype}"`);
    usedPhenotypes.add(row.phenotype);
  }
  for (const p of phenotypes) if (!usedPhenotypes.has(p)) bad(`phenotype "${p}" is never used`);
  for (const c of conditions) if (!usedConditions.has(c)) bad(`condition "${c}" is never used`);
  for (const c of g.conditions ?? []) if (!SEVERITY.includes(c.severity)) bad(`condition "${c.id}": severity "${c.severity}"`);

  // An empty list is allowed: it says no figure is known, which beats an invented one.
  if (!Array.isArray(g.frequencies)) bad("no frequencies list");
  for (const t of g.frequencies ?? []) {
    if (!FREQ_EVIDENCE.includes(t.evidence)) bad(`frequencies "${t.population}": evidence "${t.evidence}"`);
    let sum = 0;
    for (const row of t.genotypes ?? []) {
      checkPair(row.alleles, `frequencies "${t.population}"`);
      if (!seen.has(key(row.alleles))) bad(`frequencies "${t.population}": ${key(row.alleles)} is not a genotype`);
      sum += row.percent;
    }
    if (Math.abs(sum - 100) > 0.01) bad(`frequencies "${t.population}" sum to ${sum.toFixed(4)}, not 100`);
  }
  for (const c of g.citations ?? []) {
    if (!SOURCES.includes(c.source)) bad(`citation source "${c.source}"`);
    if (!/^https?:\/\//.test(c.url ?? "")) bad(`citation url "${c.url}"`);
    if (typeof c.verified !== "boolean") bad(`citation "${c.label}" does not say whether it was verified`);
  }
}

// Interactions name real loci; the mod map agrees with the genes.
for (const g of genes.values()) {
  for (const d of g.interactions?.depends_on ?? []) {
    if (!genes.has(d)) problems.push(`genes/${g.id}.json: depends on unknown locus "${d}"`);
  }
}
try {
  const map = JSON.parse(readFileSync(join(root, "mod-map.json"), "utf8"));
  const mapped = new Set();
  for (const row of map.loci) {
    mapped.add(row.locus);
    const g = genes.get(row.locus);
    if (!g) {
      problems.push(`mod-map.json: locus "${row.locus}" has no gene file`);
      continue;
    }
    // Alleles this data has and the mod lacks are appended after the mod's own, so the mod's
    // order must still be the start of the list here.
    const notInMod = row.not_in_mod ?? [];
    for (const a of notInMod) {
      if (!g.alleles.some((x) => x.id === a)) problems.push(`mod-map.json: "${row.locus}" not_in_mod names unknown allele "${a}"`);
      if (row.mod_allele_order.includes(a)) problems.push(`mod-map.json: "${row.locus}" allele "${a}" is both in the mod's order and not_in_mod`);
    }
    const undocumented = new Set(g.phenotypes.filter((p) => p.documented === false).map((p) => p.id));
    for (const r of g.genotypes) {
      const waived = (row.mod_outcome_unsupported ?? []).some((p) => key(p) === key(r.alleles));
      if (undocumented.has(r.phenotype) && !waived && !r.alleles.some((a) => notInMod.includes(a))) {
        problems.push(`genes/${g.id}.json: ${key(r.alleles)} is marked undocumented but the mod has both alleles`);
      }
    }
    const all = g.alleles.map((a) => a.id);
    const here = all.filter((a) => !notInMod.includes(a));
    if (all.slice(here.length).some((a) => !notInMod.includes(a))) problems.push(`mod-map.json: "${row.locus}" has an allele the mod lacks ahead of one it has`);
    const mod = row.mod_allele_order.filter((a) => !(row.mod_only_alleles ?? []).includes(a));
    if (here.join(",") !== mod.join(",")) {
      problems.push(`mod-map.json: "${row.locus}" allele order is [${here}] here but the mod requires [${mod}]`);
    }
  }
  for (const id of genes.keys()) if (!mapped.has(id)) problems.push(`mod-map.json: no row for "${id}"`);
} catch (e) {
  problems.push(`mod-map.json: ${e.message}`);
}

// Breeds (knowledge/format.txt, BREEDS). Every pool names a real locus and pairs it can have.
let breedCount = 0;
let breedFiles = [];
try {
  breedFiles = readdirSync(join(root, "breeds")).filter((f) => f.endsWith(".json")).sort();
} catch {
  // No breeds folder yet is not an error.
}
const pairsOf = new Map([...genes.values()].map((g) => [g.id, new Set((g.genotypes ?? []).map((r) => key(r.alleles)))]));
const isScore = (v) => (typeof v === "number" && v >= 1 && v <= 10) || (Array.isArray(v) && v.length === 2 && v.every((x) => typeof x === "number" && x >= 1 && x <= 10) && v[0] <= v[1]);
for (const file of breedFiles) {
  const bad = (msg) => problems.push(`breeds/${file}: ${msg}`);
  let b;
  const text = readFileSync(join(root, "breeds", file), "utf8");
  try {
    b = JSON.parse(text);
  } catch (e) {
    bad(`does not parse: ${e.message}`);
    continue;
  }
  breedCount++;
  if (b.format !== 1) bad(`format is ${b.format}, expected 1`);
  if (`${b.id}.json` !== file) bad(`id "${b.id}" does not match the file name`);
  if (b.species !== "horse") bad(`species is "${b.species}"`);
  for (const f of ["name", "country"]) if (!b[f]) bad(`"${f}" is empty`);
  // Nothing from a game: no game ids, biomes or spawn rules.
  if (/minecraft:|"(biomes|spawn[a-z_]*|price|commonness|kind)"/.test(text)) bad("has a game-only field or value");
  const s = b.scores ?? {};
  if (s.evidence !== "abstracted") bad(`scores evidence is "${s.evidence}"`);
  for (const f of ["speed", "jump", "health", "pull"]) if (!isScore(s[f])) bad(`score "${f}" is ${JSON.stringify(s[f])}`);
  const checkPools = (pools, where) => {
    if (!Array.isArray(pools)) return bad(`${where}: no pools list`);
    const loci = new Set();
    for (const p of pools) {
      const w = `${where} ${p.locus}`;
      if (!genes.has(p.locus)) { bad(`${w}: unknown locus`); continue; }
      if (loci.has(p.locus)) bad(`${w}: locus listed twice`);
      loci.add(p.locus);
      if (!FREQ_EVIDENCE.includes(p.evidence)) bad(`${w}: evidence "${p.evidence}"`);
      let sum = 0;
      const seen = new Set();
      for (const row of p.genotypes ?? []) {
        const k = Array.isArray(row.alleles) && row.alleles.length === 2 ? key(row.alleles) : "?";
        if (!pairsOf.get(p.locus).has(k)) bad(`${w}: ${k} is not a genotype of the locus`);
        if (seen.has(k)) bad(`${w}: ${k} listed twice`);
        seen.add(k);
        if (!(row.percent >= 0)) bad(`${w}: ${k} percent is ${row.percent}`);
        sum += row.percent;
      }
      if (Math.abs(sum - 100) > 0.01) bad(`${w}: sums to ${sum.toFixed(4)}, not 100`);
    }
  };
  checkPools(b.pools, "pools");
  if (b.strains !== undefined) {
    let share = 0;
    for (const st of b.strains) {
      if (!st.name) bad("a strain has no name");
      share += st.share;
      checkPools(st.pools, `strain "${st.name}"`);
    }
    if (Math.abs(share - 100) > 0.01) bad(`strain shares sum to ${share.toFixed(4)}, not 100`);
  }
  if (!Array.isArray(b.citations)) bad("no citations list");
  for (const c of b.citations ?? []) {
    if (!SOURCES.includes(c.source)) bad(`citation source "${c.source}"`);
    if (!/^https?:\/\//.test(c.url ?? "")) bad(`citation url "${c.url}"`);
    if (typeof c.verified !== "boolean") bad(`citation "${c.label}" does not say whether it was verified`);
  }
}

if (problems.length) {
  console.log(problems.join("\n"));
  console.log(`\n${problems.length} problem(s) in ${genes.size} gene files and ${breedCount} breed files`);
  process.exit(1);
}
console.log(`OK - ${genes.size} gene files, ${breedCount} breed files and mod-map.json pass every check`);
