#!/usr/bin/env node
// Writes breeds/<id>.json from the mod's breed files. Run from this repo's root:
//
//   node knowledge/how-this-was-exported/export-breeds.mjs <the mod's breeds folder>
//
// The folder is common/src/main/resources/horsegenetics/breeds in the mod. It holds one JSON
// per breed and an index.json. Only natural breeds are written: a file with "kind": "magical"
// is skipped. What is kept and what is dropped is listed in knowledge/format.txt (BREEDS).
// Gene keys are translated to this repo's locus ids through mod-map.json. Allele tokens are
// the same in both. WARNING: overwrites every file in breeds/.
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { join } from "node:path";

const src = process.argv[2];
if (!src) throw new Error("usage: export-breeds.mjs <the mod's breeds folder>");
const locusOf = Object.fromEntries(JSON.parse(readFileSync("mod-map.json", "utf8")).loci.map((r) => [r.mod_key, r.locus]));
// Pools the decisions leave out (climate tolerance, knowledge/decisions.txt).
const DROPPED = new Set(["horsegenetics.magic_heat", "horsegenetics.magic_cold"]);
const str = JSON.stringify;
// A score is a number, or [low, high] where the mod gives a range (welsh_pony speed).
const sc = (v) => (Array.isArray(v) ? `[${v.join(", ")}]` : v);

// The mod stores relative weights. Where they do not already sum to 100 they are scaled.
const toPercent = (weights) => {
  const sum = weights.reduce((a, b) => a + b, 0);
  if (Math.abs(sum - 100) <= 0.01) return { percents: weights, scaled: false };
  const p = weights.map((w) => Math.round((w * 1e6) / sum) / 1e4);
  const off = Math.round((100 - p.reduce((a, b) => a + b, 0)) * 1e4) / 1e4;
  p[p.indexOf(Math.max(...p))] = Math.round((Math.max(...p) + off) * 1e4) / 1e4;
  return { percents: p, scaled: true };
};
const scaled = [];
const pools = (genes, where, indent) => {
  const out = [];
  for (const [key, rows] of Object.entries(genes)) {
    if (DROPPED.has(key)) continue;
    const locus = locusOf[key];
    if (!locus) throw new Error(`${where}: no locus for ${key}`);
    const { percents, scaled: s } = toPercent(rows.map((r) => r.weight));
    if (s) scaled.push(`${where}: ${locus}`);
    const lines = rows.map((r, i) => `${indent}      { "alleles": ["${r.pair.split("/").join('", "')}"], "percent": ${percents[i]} }`);
    out.push(`${indent}  {\n${indent}    "locus": "${locus}",\n${indent}    "evidence": "estimated",\n${indent}    "genotypes": [\n${lines.join(",\n")}\n${indent}    ]\n${indent}  }`);
  }
  return out.join(",\n");
};

mkdirSync("breeds", { recursive: true });
let n = 0;
for (const f of JSON.parse(readFileSync(join(src, "index.json"), "utf8"))) {
  const b = JSON.parse(readFileSync(join(src, f), "utf8"));
  if (b.kind === "magical") continue;
  const parts = [
    `  "format": 1`, `  "id": "${b.id}"`, `  "species": "horse"`, `  "name": ${str(b.name)}`, `  "country": ${str(b.country)}`,
  ];
  if (b.description) parts.push(`  "description": ${str(b.description)}`);
  const s = b.stats;
  parts.push(`  "scores": { "evidence": "abstracted", "speed": ${sc(s.speed)}, "jump": ${sc(s.jump)}, "health": ${sc(s.health)}, "pull": ${sc(s.pull)} }`);
  parts.push(`  "pools": [\n${pools(b.genes, b.id, "  ")}\n  ]`);
  if (b.strains) {
    const { percents } = toPercent(b.strains.map((x) => x.weight));
    parts.push(`  "strains": [\n${b.strains.map((x, i) => `    {\n      "name": ${str(x.name)},\n      "share": ${percents[i]},\n      "pools": [\n${pools(x.genes, `${b.id} strain ${x.name}`, "      ")}\n      ]\n    }`).join(",\n")}\n  ]`);
  }
  parts.push(`  "citations": []`);
  writeFileSync(join("breeds", `${b.id}.json`), `{\n${parts.join(",\n")}\n}\n`);
  n++;
}
console.log(`${n} breeds written`);
if (scaled.length) console.log(`pools scaled to 100:\n  ${scaled.join("\n  ")}`);
