// Scratch: copy each exported gene's wiki Science tab to knowledge/science/, keeping link URLs,
// and write the external links into the gene file as citations. Also fills gene symbols.
import { readFileSync, writeFileSync, readdirSync, existsSync, mkdirSync, cpSync } from "node:fs";
import { join } from "node:path";

const [, , exportDir, wikiDir, outDir] = process.argv;
mkdirSync(join(outDir, "genes"), { recursive: true });
mkdirSync(join(outDir, "knowledge", "science"), { recursive: true });

// Gene symbols the export author is confident of. Anything else stays null for the audit.
const SYMBOL = {
  extension: "MC1R", agouti: "ASIP", matp: "SLC45A2", champagne: "SLC36A1", silver: "PMEL", dun: "TBX3",
  grey: "STX17", roan: "KIT", tobiano: "KIT", kit: "KIT", leopard: "TRPM1", patn1: "RFWD3", mitf: "MITF",
  pax3: "PAX3", ednrb: "EDNRB", mushroom: "MFSD12", tiger_eye: "SLC24A5", brindle: "MBTPS2",
  mstn: "MSTN", pdk4: "PDK4", ckm: "CKM", ryr2: "RYR2", lcorl: "LCORL", hmga2: "HMGA2", dmrt3: "DMRT3",
  acan: "ACAN", b4galt7: "B4GALT7", plod1: "PLOD1", rapgef5: "RAPGEF5", st14: "ST14", shox: "SHOX",
  met: "MET", ppib: "PPIB", prkdc: "PRKDC", myo5a: "MYO5A", toe1: "TOE1", gbe1: "GBE1", slc5a3: "SLC5A3",
  lamc2: "LAMC2", lama3: "LAMA3", scn4a: "SCN4A", gys1: "GYS1", ryr1: "RYR1",
};

const ENT = { amp: "&", lt: "<", gt: ">", quot: '"', apos: "'", nbsp: " ", mdash: "-", ndash: "-", rsquo: "'",
  lsquo: "'", rdquo: '"', ldquo: '"', hellip: "...", times: "x", rarr: "->", larr: "<-", middot: "-",
  deg: " deg", plusmn: "+/-", frac12: "1/2", ge: ">=", le: "<=", minus: "-", asymp: "~", micro: "u", alpha: "α", beta: "β", ntilde: "ñ", eacute: "é", prime: "′", ouml: "ö", ccedil: "ç", auml: "ä" };
const decode = (s) => s
  .replace(/&#(\d+);/g, (_, n) => String.fromCodePoint(+n))
  .replace(/&#x([0-9a-f]+);/gi, (_, n) => String.fromCodePoint(parseInt(n, 16)))
  .replace(/&([a-z0-9]+);/gi, (m, n) => (n in ENT ? ENT[n] : m));

function sourceOf(url) {
  if (/vgl\.ucdavis\.edu/.test(url)) return "ucdavis-vgl";
  if (/omia\.org/.test(url)) return "omia";
  if (/ncbi\.nlm\.nih\.gov|pubmed/.test(url)) return "pubmed";
  if (/doi\.org|journals\.|wiley|springer|nature\.com|sciencedirect|plos|biomedcentral|oup\.com|mdpi/.test(url)) return "paper";
  return "other";
}

function scienceTab(html) {
  const start = html.indexOf('<section class="tab-panel" data-tab="science">');
  if (start < 0) return null;
  const end = html.indexOf("</section>", start);
  return html.slice(start, end).replace(/^<section[^>]*>/, "");
}

function toText(html, citations) {
  let s = html.replace(/\r/g, "").replace(/<!--[\s\S]*?-->/g, "").replace(/\s+/g, " ");
  s = s.replace(/<a\s[^>]*href="([^"]+)"[^>]*>([\s\S]*?)<\/a>/gi, (_, href, text) => {
    const label = decode(text.replace(/<[^>]+>/g, "")).trim();
    if (/^https?:/.test(href)) {
      const url = decode(href);
      if (!citations.some((c) => c.url === url)) citations.push({ source: sourceOf(url), label, url });
      return `${label} (${url})`;
    }
    return label;
  });
  s = s.replace(/<h[1-6][^>]*>([\s\S]*?)<\/h[1-6]>/gi, (_, t) => {
    const title = decode(t.replace(/<[^>]+>/g, "")).trim();
    return `\n\n${title}\n${"-".repeat(title.length)}\n`;
  });
  s = s.replace(/<\/t[dh]>\s*<t[dh][^>]*>/gi, " | ").replace(/<tr[^>]*>/gi, "\n").replace(/<\/tr>/gi, "");
  s = s.replace(/<li[^>]*>/gi, "\n- ").replace(/<\/(p|div|ul|ol|table|dl|dd|dt|blockquote|figure)>/gi, "\n\n")
    .replace(/<(p|div|ul|ol|table|dl|dd|dt|blockquote|br)[^>]*>/gi, "\n");
  s = decode(s.replace(/<[^>]+>/g, ""));
  return s.split("\n").map((l) => l.replace(/[ \t]+/g, " ").trim()).join("\n").replace(/\n{3,}/g, "\n\n").trim();
}

const noScience = [];
let cited = 0;
for (const file of readdirSync(join(exportDir, "genes")).sort()) {
  const id = file.replace(/\.json$/, "");
  const gene = JSON.parse(readFileSync(join(exportDir, "genes", file), "utf8"));
  const citations = [];
  const page = join(wikiDir, `gene-${id.replaceAll("_", "-")}.html`);
  const tab = existsSync(page) ? scienceTab(readFileSync(page, "utf8")) : null;
  if (tab) {
    const text = toText(tab, citations);
    writeFileSync(join(outDir, "knowledge", "science", `${id}.txt`),
      `${gene.name} - science notes\n(Copied 2026-10-12 from the Science tab of the Procedural Horse Coats wiki page for this gene.\n`
      + `Written for that project: it may mention how a game models the gene. Not yet audited.)\n\n${text}\n`);
  } else {
    noScience.push(id);
  }
  // Insert symbol and citations by text, so the hand-laid-out file keeps its shape.
  let raw = readFileSync(join(exportDir, "genes", file), "utf8");
  raw = raw.replace('"symbol": null', `"symbol": ${id in SYMBOL ? JSON.stringify(SYMBOL[id]) : "null"}`);
  if (citations.length) {
    cited++;
    const rows = citations.map((c) => `    { "source": ${JSON.stringify(c.source)}, "label": ${JSON.stringify(c.label)}, "url": ${JSON.stringify(c.url)} }`);
    raw = raw.replace('"citations": []', `"citations": [\n${rows.join(",\n")}\n  ]`);
  }
  JSON.parse(raw);
  writeFileSync(join(outDir, "genes", file), raw);
}
cpSync(join(exportDir, "mod-map.json"), join(outDir, "mod-map.json"));
console.log(`genes with a science tab: ${68 - noScience.length}; with citations: ${cited}`);
console.log(`no science tab: ${noScience.join(", ")}`);
