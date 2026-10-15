// A short tour of the reader. Run it with the path of the repository folder:
//
//     example ../..

#include <cstdio>
#include <iostream>

#include "open_equine_genetics.hpp"

int main(int argc, char** argv) {
    oeg::Database db = oeg::Database::load(argc > 1 ? argv[1] : "../..");
    std::cout << db.genes.size() << " loci, " << db.breeds.size() << " breeds\n\n";

    // What one pair does.
    const oeg::Gene* extension = db.gene("extension");
    auto o = extension->outcome("E", "e");
    std::cout << "E/e at extension: " << o->phenotype->name << (o->viable ? "" : " (not viable)") << "\n";
    for (const auto& c : o->conditions) std::cout << "  condition: " << c.name << " (" << c.severity << ")\n";

    // The foals of two E/e parents, with their chances.
    std::cout << "\nE/e x E/e at extension:\n";
    for (const auto& child : extension->cross({"E", "e"}, {"E", "e"})) {
        std::printf("  %s/%s  %3.0f%%  %s\n", child.alleles.first.c_str(), child.alleles.second.c_str(),
                    child.probability * 100, child.outcome->phenotype->name.c_str());
    }

    // A random Arabian mare, and what shows on her coat.
    oeg::Horse mare = db.random_horse("arabian", oeg::MARE);
    std::cout << "\nA random Arabian mare" << (mare.strain.empty() ? "" : " of the " + mare.strain + " strain")
              << ":\n";
    for (const auto& [locus, outcome] : db.outcomes(mare.genotype)) {
        if (outcome && outcome->phenotype && outcome->phenotype->changes_coat) {
            const oeg::Pair& p = mare.genotype[locus];
            std::cout << "  " << locus << " " << p.first << "/" << p.second << ": " << outcome->phenotype->name
                      << "\n";
        }
    }
    return 0;
}
