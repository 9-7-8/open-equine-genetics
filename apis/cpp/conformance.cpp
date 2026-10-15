// Prints the conformance report described in apis/README.md. Every port prints the same.
//
//     conformance ../..

#include <cstdio>
#include <iostream>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "open_equine_genetics.hpp"

static std::string pair(const oeg::Pair& p) { return p.first + "/" + p.second; }

static std::string phen(const std::optional<oeg::Outcome>& o) {
    return o && o->phenotype ? o->phenotype->id : "-";
}

static bool has(const oeg::Pair& p, const std::string& allele) { return p.first == allele || p.second == allele; }

int main(int argc, char** argv) {
    try {
        oeg::Database db = oeg::Database::load(argc > 1 ? argv[1] : "../..");
        std::string out;
        auto line = [&](const std::string& s) { out += s; out += '\n'; };
        for (const auto& [id, gene] : db.genes) {
            line("gene " + id + " " + gene.inheritance + " " + gene.evidence + " alleles=" +
                 std::to_string(gene.alleles.size()));
            std::vector<oeg::GenotypeRow> rows = gene.genotypes;
            rows.insert(rows.end(), gene.nonviable_genotypes.begin(), gene.nonviable_genotypes.end());
            for (const auto& row : rows) {
                auto o = gene.outcome(row.alleles.second, row.alleles.first);
                std::string conditions;
                for (const auto& c : o->conditions) conditions += (conditions.empty() ? "" : ",") + c.id;
                line("  outcome " + pair(o->alleles) + " -> " + phen(o) + " [" + conditions + "] viable=" +
                     (o->viable ? "true" : "false"));
            }
            if (gene.interactions) {
                for (const auto& entry : gene.interactions->table) {
                    oeg::Genotype context(entry.with.begin(), entry.with.end());
                    auto o = gene.outcome(entry.alleles.first, entry.alleles.second, context);
                    std::string with;
                    for (const auto& [locus, p] : entry.with) with += (with.empty() ? "" : ",") + locus + "=" + pair(p);
                    line("  with " + with + " : " + pair(o->alleles) + " -> " + phen(o));
                }
            }
            // The dam is the first mare's pair, the sire the last stallion's, heterozygous where possible.
            const oeg::Allele* placeholder = gene.placeholder();
            std::vector<oeg::Pair> mares, sires, mares_het, sires_het;
            for (const auto& row : gene.genotypes) {
                const oeg::Pair& p = row.alleles;
                bool het = p.first != p.second;
                if (!placeholder || !has(p, placeholder->id)) { mares.push_back(p); if (het) mares_het.push_back(p); }
                if (!placeholder || has(p, placeholder->id)) { sires.push_back(p); if (het) sires_het.push_back(p); }
            }
            oeg::Pair dam = (mares_het.empty() ? mares : mares_het).front();
            oeg::Pair sire = (sires_het.empty() ? sires : sires_het).back();
            line("  cross " + pair(dam) + " x " + pair(sire));
            for (const auto& child : gene.cross(dam, sire)) {
                char prob[32];
                std::snprintf(prob, sizeof prob, "%.6f", child.probability);
                line("    " + pair(child.alleles) + " " + prob + " " + phen(child.outcome));
            }
        }
        std::size_t i = 0;
        for (const auto& [id, breed] : db.breeds) {
            line("breed " + id + " strains=" + std::to_string(breed.strains.size()));
            std::vector<std::string> sexes{oeg::MARE};
            if (i < 3) sexes.push_back(oeg::STALLION);
            for (const auto& sex : sexes) {
                oeg::Horse horse = db.random_horse(id, sex, "", [] { return 0.5; });
                line("  horse " + horse.sex + " strain=" + (horse.strain.empty() ? "-" : horse.strain));
                auto outcomes = db.outcomes(horse.genotype);
                for (const auto& [locus, p] : horse.genotype) line("    " + locus + " " + pair(p) + " " + phen(outcomes[locus]));
            }
            ++i;
        }
#ifdef _WIN32
        _setmode(_fileno(stdout), _O_BINARY);
#endif
        std::fwrite(out.data(), 1, out.size(), stdout);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
}
