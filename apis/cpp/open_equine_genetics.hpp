// Open Equine Genetics: a small reader for the genes/ and breeds/ JSON files.
//
// One header, C++17, standard library only. Copy it into your project.
//
//     auto db = oeg::Database::load("path/to/open-equine-genetics");
//     db.gene("extension")->outcome("E", "e")->phenotype->name;    // "Wild type"
//
// The API is the same in every language in apis/; apis/README.md describes it.
// Where a breed, strain or sex is optional, an empty string means "none".

#ifndef OPEN_EQUINE_GENETICS_HPP
#define OPEN_EQUINE_GENETICS_HPP

#include <algorithm>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace oeg {

namespace detail {

// A parsed JSON value. Objects keep their keys in file order.
struct Json {
    enum class Type { null, boolean, number, string, array, object };
    Type type = Type::null;
    bool boolean = false;
    double number = 0.0;
    std::string string;
    std::vector<Json> items;        // array elements
    std::vector<std::string> keys;  // object keys, in file order
    std::vector<Json> values;       // object values, matching keys

    bool is_null() const { return type == Type::null; }

    const Json* find(const std::string& key) const {
        for (std::size_t i = 0; i < keys.size(); ++i)
            if (keys[i] == key) return &values[i];
        return nullptr;
    }
};

class JsonParser {
public:
    explicit JsonParser(const std::string& text) : s_(text) {}

    Json parse() {
        if (s_.compare(0, 3, "\xEF\xBB\xBF") == 0) i_ = 3;
        skip_ws();
        Json v = value();
        skip_ws();
        if (i_ != s_.size()) fail("unexpected text after the value");
        return v;
    }

private:
    const std::string& s_;
    std::size_t i_ = 0;

    [[noreturn]] void fail(const std::string& what) const {
        throw std::runtime_error("bad JSON at byte " + std::to_string(i_) + ": " + what);
    }

    void skip_ws() {
        while (i_ < s_.size() && (s_[i_] == ' ' || s_[i_] == '\t' || s_[i_] == '\n' || s_[i_] == '\r')) ++i_;
    }

    char peek() const { return i_ < s_.size() ? s_[i_] : '\0'; }

    void expect(char c) {
        if (peek() != c) fail(std::string("expected '") + c + "'");
        ++i_;
    }

    bool word(const char* w) {
        std::size_t n = std::strlen(w);
        if (s_.compare(i_, n, w) != 0) return false;
        i_ += n;
        return true;
    }

    Json value() {
        Json v;
        char c = peek();
        if (c == '{') {
            ++i_;
            v.type = Json::Type::object;
            skip_ws();
            if (peek() == '}') { ++i_; return v; }
            for (;;) {
                skip_ws();
                if (peek() != '"') fail("expected a key");
                v.keys.push_back(string());
                skip_ws();
                expect(':');
                skip_ws();
                v.values.push_back(value());
                skip_ws();
                if (peek() == ',') { ++i_; continue; }
                expect('}');
                return v;
            }
        }
        if (c == '[') {
            ++i_;
            v.type = Json::Type::array;
            skip_ws();
            if (peek() == ']') { ++i_; return v; }
            for (;;) {
                skip_ws();
                v.items.push_back(value());
                skip_ws();
                if (peek() == ',') { ++i_; continue; }
                expect(']');
                return v;
            }
        }
        if (c == '"') { v.type = Json::Type::string; v.string = string(); return v; }
        if (word("true")) { v.type = Json::Type::boolean; v.boolean = true; return v; }
        if (word("false")) { v.type = Json::Type::boolean; return v; }
        if (word("null")) return v;
        if (c == '-' || (c >= '0' && c <= '9')) { v.type = Json::Type::number; v.number = number(); return v; }
        fail("unexpected character");
    }

    double number() {
        std::size_t start = i_;
        if (peek() == '-') ++i_;
        auto digits = [&] {
            std::size_t from = i_;
            while (peek() >= '0' && peek() <= '9') ++i_;
            if (i_ == from) fail("expected a digit");
        };
        digits();
        if (peek() == '.') { ++i_; digits(); }
        if (peek() == 'e' || peek() == 'E') {
            ++i_;
            if (peek() == '+' || peek() == '-') ++i_;
            digits();
        }
        std::string text = s_.substr(start, i_ - start);
        double d = 0.0;
#if defined(__cpp_lib_to_chars)
        auto r = std::from_chars(text.data(), text.data() + text.size(), d);
        if (r.ec != std::errc() || r.ptr != text.data() + text.size()) fail("bad number");
#else
        char* end = nullptr;
        d = std::strtod(text.c_str(), &end);
        if (end != text.c_str() + text.size()) fail("bad number");
#endif
        return d;
    }

    unsigned hex4() {
        if (i_ + 4 > s_.size()) fail("short \\u escape");
        unsigned u = 0;
        for (int k = 0; k < 4; ++k) {
            char h = s_[i_++];
            u <<= 4;
            if (h >= '0' && h <= '9') u |= static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f') u |= static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F') u |= static_cast<unsigned>(h - 'A' + 10);
            else fail("bad \\u escape");
        }
        return u;
    }

    static void utf8(std::string& out, unsigned cp) {
        if (cp < 0x80) {
            out += static_cast<char>(cp);
        } else if (cp < 0x800) {
            out += static_cast<char>(0xC0 | (cp >> 6));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
            out += static_cast<char>(0xE0 | (cp >> 12));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        } else {
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
        }
    }

    std::string string() {
        expect('"');
        std::string out;
        for (;;) {
            if (i_ >= s_.size()) fail("unterminated string");
            char c = s_[i_++];
            if (c == '"') return out;
            if (static_cast<unsigned char>(c) < 0x20) fail("control character in string");
            if (c != '\\') { out += c; continue; }
            if (i_ >= s_.size()) fail("unterminated string");
            char e = s_[i_++];
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    unsigned cp = hex4();
                    // A surrogate pair is one character written as two escapes.
                    if (cp >= 0xD800 && cp < 0xDC00 && s_.compare(i_, 2, "\\u") == 0) {
                        std::size_t back = i_;
                        i_ += 2;
                        unsigned lo = hex4();
                        if (lo >= 0xDC00 && lo < 0xE000) cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        else i_ = back;
                    }
                    utf8(out, cp);
                    break;
                }
                default: fail("bad escape");
            }
        }
    }
};

inline const Json& at(const Json& obj, const std::string& key) {
    if (obj.type != Json::Type::object) throw std::runtime_error("expected an object holding \"" + key + "\"");
    const Json* v = obj.find(key);
    if (!v) throw std::runtime_error("missing \"" + key + "\"");
    return *v;
}

inline std::string str(const Json& obj, const std::string& key) {
    const Json& v = at(obj, key);
    if (v.type != Json::Type::string) throw std::runtime_error("\"" + key + "\" is not a string");
    return v.string;
}

inline std::optional<std::string> opt_str(const Json& obj, const std::string& key) {
    const Json* v = obj.find(key);
    if (!v || v->is_null()) return std::nullopt;
    if (v->type != Json::Type::string) throw std::runtime_error("\"" + key + "\" is not a string");
    return v->string;
}

inline bool boolean(const Json& obj, const std::string& key) {
    const Json& v = at(obj, key);
    if (v.type != Json::Type::boolean) throw std::runtime_error("\"" + key + "\" is not true or false");
    return v.boolean;
}

inline double num(const Json& obj, const std::string& key) {
    const Json& v = at(obj, key);
    if (v.type != Json::Type::number) throw std::runtime_error("\"" + key + "\" is not a number");
    return v.number;
}

// The array at key; an absent key gives an empty list only where the field is optional.
inline const std::vector<Json>& arr(const Json& obj, const std::string& key, bool optional = false) {
    static const std::vector<Json> none;
    const Json* v = obj.find(key);
    if (!v && optional) return none;
    if (!v) throw std::runtime_error("missing \"" + key + "\"");
    if (v->type != Json::Type::array) throw std::runtime_error("\"" + key + "\" is not a list");
    return v->items;
}

inline std::pair<std::string, std::string> pair_of(const Json& v) {
    if (v.type != Json::Type::array || v.items.size() != 2 ||
        v.items[0].type != Json::Type::string || v.items[1].type != Json::Type::string)
        throw std::runtime_error("an allele pair is not two strings");
    return {v.items[0].string, v.items[1].string};
}

}  // namespace detail

using Json = detail::Json;
using Pair = std::pair<std::string, std::string>;
using Genotype = std::map<std::string, Pair>;  // locus id -> pair
using Rng = std::function<double()>;          // returns a number in [0, 1)

inline const std::string MARE = "mare";
inline const std::string STALLION = "stallion";
inline const std::string GENERAL_POPULATION = "general population";

// True if two allele pairs are the same pair. Order within a pair carries no meaning.
inline bool same_pair(const Pair& x, const Pair& y) {
    return (x.first == y.first && x.second == y.second) || (x.first == y.second && x.second == y.first);
}

struct Allele {
    std::string id;
    std::string name;
    bool wild_type = false;
    std::optional<std::string> quality;      // "verified", "unverified", "abstracted", "invalid"; none on a placeholder
    std::optional<std::string> placeholder;  // set only on the stand-in "Y" of an x-linked locus
};

struct Phenotype {
    std::string id;
    std::string name;
    std::string description;
    bool changes_coat = false;
    bool documented = true;
};

struct Condition {
    std::string id;
    std::string name;
    std::string description;
    std::string severity;  // "informational", "impairing", "lethal_in_early_life", "lethal_at_birth", "lethal_at_conception"
};

struct GenotypeShare {
    Pair alleles;
    double percent = 0.0;
};

// A frequency table (in a gene file) or a gene pool (in a breed file).
struct GenotypeTable {
    std::string locus;
    std::optional<std::string> population;  // who a gene file's table describes; none on a breed pool
    std::string evidence;                   // "published" or "estimated"
    std::vector<GenotypeShare> genotypes;
};

struct Citation {
    std::string source;
    std::string label;
    std::string url;
    bool verified = false;
};

// One row of a gene file's "genotypes" or "nonviable_genotypes".
struct GenotypeRow {
    Pair alleles;
    std::optional<std::string> phenotype;
    std::vector<std::string> conditions;
};

struct InteractionRow {
    Pair alleles;
    std::vector<std::pair<std::string, Pair>> with;  // other locus -> pair, in file order
    std::string phenotype;
};

struct Interactions {
    std::vector<std::string> depends_on;
    std::vector<InteractionRow> table;
};

// What one allele pair at one locus does to a horse.
struct Outcome {
    Pair alleles;                        // as the gene file writes the pair
    std::optional<Phenotype> phenotype;  // none only for a nonviable pair with no phenotype given
    std::vector<Condition> conditions;
    bool viable = true;                  // false: no living horse has this pair
};

struct Offspring {
    Pair alleles;
    double probability = 0.0;        // 0 to 1
    std::optional<Outcome> outcome;  // none only if the pair is not in the gene file
};

struct Gene {
    std::string id;
    std::string name;
    std::optional<std::string> symbol;
    std::optional<std::string> chromosome;
    std::string evidence;     // "published" or "abstracted" (an abstracted locus is not a real gene)
    std::string summary;
    std::string inheritance;  // "autosomal", "x_linked", "y_linked"
    bool affects_coat = false;  // changes the hair coat
    bool affects_eye = false;   // changes how the eye is drawn; false where not given
    std::vector<Allele> alleles;
    std::vector<Phenotype> phenotypes;
    std::vector<Condition> conditions;
    std::vector<GenotypeTable> frequencies;
    std::vector<Citation> citations;
    std::vector<GenotypeRow> genotypes;
    std::vector<GenotypeRow> nonviable_genotypes;
    std::optional<Interactions> interactions;
    Json raw;  // the whole file, for fields this reader does not model

    const Allele* allele(const std::string& allele_id) const {
        for (const auto& a : alleles) if (a.id == allele_id) return &a;
        return nullptr;
    }

    const Phenotype* phenotype(const std::string& phenotype_id) const {
        for (const auto& p : phenotypes) if (p.id == phenotype_id) return &p;
        return nullptr;
    }

    const Condition* condition(const std::string& condition_id) const {
        for (const auto& c : conditions) if (c.id == condition_id) return &c;
        return nullptr;
    }

    const Allele* wild_type() const {
        for (const auto& a : alleles) if (a.wild_type) return &a;
        return nullptr;
    }

    const Allele* placeholder() const {
        for (const auto& a : alleles) if (a.placeholder && !a.placeholder->empty()) return &a;
        return nullptr;
    }

    // The outcome of the pair a/b, or none if it is not a pair of this locus.
    std::optional<Outcome> outcome(const std::string& a, const std::string& b) const {
        return find_outcome({a, b}, nullptr);
    }

    // The same, where context gives the pairs at other loci. Where this locus depends on
    // others and context gives all of them, the interaction table decides the phenotype;
    // otherwise the result is the outcome with the other loci at wild type.
    std::optional<Outcome> outcome(const std::string& a, const std::string& b, const Genotype& context) const {
        return find_outcome({a, b}, &context);
    }

    // Every pair a foal of this dam and sire can get at this locus, with its chance.
    // Each parent passes one of its two alleles, each with chance 1/2. On an x-linked
    // locus the sire's placeholder "Y" stands for passing his Y chromosome: those foals
    // are colts. Nonviable pairs are included, marked by outcome->viable.
    std::vector<Offspring> cross(const Pair& dam, const Pair& sire) const {
        std::vector<Offspring> result;
        for (const std::string& x : {dam.first, dam.second}) {
            for (const std::string& y : {sire.first, sire.second}) {
                Pair p{x, y};
                bool listed = false;
                for (auto& o : result) {
                    if (same_pair(o.alleles, p)) { o.probability += 0.25; listed = true; break; }
                }
                if (listed) continue;
                Offspring o;
                o.outcome = outcome(x, y);
                o.alleles = o.outcome ? o.outcome->alleles : p;
                o.probability = 0.25;
                result.push_back(std::move(o));
            }
        }
        return result;
    }

    const GenotypeTable* frequency_table(const std::string& population = GENERAL_POPULATION) const {
        for (const auto& t : frequencies) if (t.population && *t.population == population) return &t;
        return nullptr;
    }

private:
    std::optional<Outcome> find_outcome(const Pair& p, const Genotype* context) const {
        for (const auto& row : genotypes)
            if (same_pair(row.alleles, p)) return make_outcome(row, true, context);
        for (const auto& row : nonviable_genotypes)
            if (same_pair(row.alleles, p)) return make_outcome(row, false, context);
        return std::nullopt;
    }

    Outcome make_outcome(const GenotypeRow& row, bool viable, const Genotype* context) const {
        std::optional<std::string> phenotype_id = row.phenotype;
        if (viable && interactions && context && !context->empty() && depends_met(*context)) {
            for (const auto& entry : interactions->table) {
                if (!same_pair(entry.alleles, row.alleles)) continue;
                bool all = true;
                for (const auto& w : entry.with) {
                    auto it = context->find(w.first);
                    if (it == context->end() || !same_pair(w.second, it->second)) { all = false; break; }
                }
                if (all) { phenotype_id = entry.phenotype; break; }
            }
        }
        Outcome o;
        o.alleles = row.alleles;
        if (phenotype_id && !phenotype_id->empty())
            if (const Phenotype* ph = phenotype(*phenotype_id)) o.phenotype = *ph;
        for (const auto& c : row.conditions)
            if (const Condition* cond = condition(c)) o.conditions.push_back(*cond);
        o.viable = viable;
        return o;
    }

    bool depends_met(const Genotype& context) const {
        for (const auto& locus : interactions->depends_on)
            if (context.find(locus) == context.end()) return false;
        return true;
    }
};

struct Strain {
    std::string name;
    double share = 0.0;
    std::vector<GenotypeTable> pools;
};

struct Breed {
    std::string id;
    std::string name;
    std::string country;
    std::optional<std::string> description;
    Json scores;  // estimated 1-10 scores, not measurements; evidence is always "abstracted"
    std::vector<GenotypeTable> pools;
    std::vector<Strain> strains;
    std::vector<Citation> citations;
    Json raw;

    const Strain* strain(const std::string& name_) const {
        for (const auto& s : strains) if (s.name == name_) return &s;
        return nullptr;
    }

    // The breed's gene pool at a locus: the strain's if it has one there, else the breed's.
    const GenotypeTable* pool(const std::string& locus, const std::string& strain_name = "") const {
        const Strain* s = strain_name.empty() ? nullptr : strain(strain_name);
        if (s)
            for (const auto& p : s->pools) if (p.locus == locus) return &p;
        for (const auto& p : pools) if (p.locus == locus) return &p;
        return nullptr;
    }
};

struct Horse {
    std::string sex;     // MARE or STALLION
    std::string breed;   // empty: the general population
    std::string strain;  // empty: none
    Genotype genotype;   // every locus in the database
};

// Draw one pair from a table. rng returns a number in [0, 1).
inline std::optional<Pair> sample(const GenotypeTable& table, const Rng& rng) {
    double total = 0.0;
    for (const auto& g : table.genotypes) total += g.percent;
    if (total <= 0) return std::nullopt;
    double target = rng() * total;
    double running = 0.0;
    for (const auto& g : table.genotypes) {
        running += g.percent;
        if (target < running) return g.alleles;
    }
    return table.genotypes.back().alleles;
}

// A default rng: std::mt19937, giving 53-bit doubles in [0, 1) the way Python does.
inline Rng default_rng(std::uint32_t seed) {
    auto gen = std::make_shared<std::mt19937>(seed);
    return [gen]() {
        std::uint32_t a = (*gen)() >> 5, b = (*gen)() >> 6;
        return (a * 67108864.0 + b) / 9007199254740992.0;
    };
}

inline Rng default_rng() { return default_rng(std::random_device{}()); }

namespace detail {

inline GenotypeTable table(const Json& d, const std::string& locus, std::optional<std::string> population) {
    GenotypeTable t;
    t.locus = locus;
    t.population = std::move(population);
    t.evidence = str(d, "evidence");
    for (const auto& g : arr(d, "genotypes")) t.genotypes.push_back({pair_of(at(g, "alleles")), num(g, "percent")});
    return t;
}

inline std::vector<Citation> citations(const Json& d) {
    std::vector<Citation> out;
    for (const auto& c : arr(d, "citations", true))
        out.push_back({str(c, "source"), str(c, "label"), str(c, "url"), boolean(c, "verified")});
    return out;
}

inline std::vector<GenotypeRow> rows(const Json& d, const std::string& key, bool optional) {
    std::vector<GenotypeRow> out;
    for (const auto& r : arr(d, key, optional)) {
        GenotypeRow row;
        row.alleles = pair_of(at(r, "alleles"));
        row.phenotype = opt_str(r, "phenotype");
        for (const auto& c : arr(r, "conditions", true)) {
            if (c.type != Json::Type::string) throw std::runtime_error("a condition id is not a string");
            row.conditions.push_back(c.string);
        }
        out.push_back(std::move(row));
    }
    return out;
}

inline Gene gene(Json d) {
    Gene g;
    g.id = str(d, "id");
    g.name = str(d, "name");
    g.symbol = opt_str(d, "symbol");
    g.chromosome = opt_str(d, "chromosome");
    g.evidence = str(d, "evidence");
    g.summary = str(d, "summary");
    g.inheritance = str(d, "inheritance");
    g.affects_coat = boolean(d, "affects_coat");
    g.affects_eye = d.find("affects_eye") ? boolean(d, "affects_eye") : false;
    for (const auto& a : arr(d, "alleles"))
        g.alleles.push_back({str(a, "id"), str(a, "name"), boolean(a, "wild_type"), opt_str(a, "quality"),
                             opt_str(a, "placeholder")});
    for (const auto& p : arr(d, "phenotypes")) {
        const Json* documented = p.find("documented");
        g.phenotypes.push_back({str(p, "id"), str(p, "name"), str(p, "description"), boolean(p, "changes_coat"),
                                documented ? boolean(p, "documented") : true});
    }
    for (const auto& c : arr(d, "conditions", true))
        g.conditions.push_back({str(c, "id"), str(c, "name"), str(c, "description"), str(c, "severity")});
    for (const auto& t : arr(d, "frequencies")) g.frequencies.push_back(table(t, g.id, str(t, "population")));
    g.citations = citations(d);
    g.genotypes = rows(d, "genotypes", false);
    g.nonviable_genotypes = rows(d, "nonviable_genotypes", true);
    if (const Json* in = d.find("interactions"); in && !in->is_null()) {
        Interactions x;
        for (const auto& l : arr(*in, "depends_on")) {
            if (l.type != Json::Type::string) throw std::runtime_error("a depends_on entry is not a string");
            x.depends_on.push_back(l.string);
        }
        for (const auto& e : arr(*in, "table")) {
            InteractionRow row;
            row.alleles = pair_of(at(e, "alleles"));
            const Json& with = at(e, "with");
            if (with.type != Json::Type::object) throw std::runtime_error("\"with\" is not an object");
            for (std::size_t i = 0; i < with.keys.size(); ++i) row.with.emplace_back(with.keys[i], pair_of(with.values[i]));
            row.phenotype = str(e, "phenotype");
            x.table.push_back(std::move(row));
        }
        g.interactions = std::move(x);
    }
    g.raw = std::move(d);
    return g;
}

inline Breed breed(Json d) {
    Breed b;
    b.id = str(d, "id");
    b.name = str(d, "name");
    b.country = str(d, "country");
    b.description = opt_str(d, "description");
    b.scores = at(d, "scores");
    for (const auto& p : arr(d, "pools")) b.pools.push_back(table(p, str(p, "locus"), std::nullopt));
    for (const auto& s : arr(d, "strains", true)) {
        Strain st;
        st.name = str(s, "name");
        st.share = num(s, "share");
        for (const auto& p : arr(s, "pools")) st.pools.push_back(table(p, str(p, "locus"), std::nullopt));
        b.strains.push_back(std::move(st));
    }
    b.citations = citations(d);
    b.raw = std::move(d);
    return b;
}

// Calls make(json) for every .json file in a folder, in file-name order.
template <typename F>
void read_dir(const std::filesystem::path& dir, F make) {
    std::vector<std::string> names;
    std::error_code ec;
    for (std::filesystem::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        std::string name = it->path().filename().string();
        if (name.size() >= 5 && name.compare(name.size() - 5, 5, ".json") == 0) names.push_back(name);
    }
    if (ec) throw std::runtime_error(dir.string() + ": " + ec.message());
    std::sort(names.begin(), names.end());
    for (const auto& name : names) {
        std::filesystem::path file = dir / name;
        try {
            std::ifstream in(file, std::ios::binary);
            if (!in) throw std::runtime_error("cannot open the file");
            std::ostringstream text;
            text << in.rdbuf();
            std::string s = text.str();
            make(JsonParser(s).parse());
        } catch (const std::exception& e) {
            throw std::runtime_error(file.string() + ": " + e.what());
        }
    }
}

}  // namespace detail

class Database {
public:
    std::map<std::string, Gene> genes;    // id -> Gene, sorted by id
    std::map<std::string, Breed> breeds;  // id -> Breed, sorted by id

    // Read every file in <root>/genes and <root>/breeds. root is the repository folder.
    static Database load(const std::filesystem::path& root) {
        Database db;
        detail::read_dir(root / "genes", [&](detail::Json d) {
            Gene g = detail::gene(std::move(d));
            std::string id = g.id;
            db.genes.insert_or_assign(id, std::move(g));
        });
        detail::read_dir(root / "breeds", [&](detail::Json d) {
            Breed b = detail::breed(std::move(d));
            std::string id = b.id;
            db.breeds.insert_or_assign(id, std::move(b));
        });
        return db;
    }

    const Gene* gene(const std::string& id) const {
        auto it = genes.find(id);
        return it == genes.end() ? nullptr : &it->second;
    }

    const Breed* breed(const std::string& id) const {
        auto it = breeds.find(id);
        return it == breeds.end() ? nullptr : &it->second;
    }

    // Where a horse's pair at a locus is drawn from: the breed (or strain) pool,
    // else the gene's general-population table, else nullptr.
    const GenotypeTable* genotype_table(const std::string& locus, const std::string& breed_id = "",
                                        const std::string& strain = "") const {
        const Breed* b = breed_id.empty() ? nullptr : breed(breed_id);
        const GenotypeTable* t = b ? b->pool(locus, strain) : nullptr;
        if (!t)
            if (const Gene* g = gene(locus)) t = g->frequency_table();
        return t;
    }

    // A random horse: a pair at every locus. With no breed, the general population.
    // If the breed has strains and none is named, one is drawn by share. A locus with
    // no table gets the wild-type pair. With no rng, a fresh default_rng() is used.
    Horse random_horse(const std::string& breed_id = "", const std::string& sex = MARE,
                       const std::string& strain = "", Rng rng = nullptr) const {
        if (!rng) rng = default_rng();
        Horse h{sex, breed_id, strain, {}};
        const Breed* b = breed_id.empty() ? nullptr : breed(breed_id);
        if (b && !b->strains.empty() && strain.empty()) {
            GenotypeTable shares;
            shares.evidence = "estimated";
            for (const auto& s : b->strains) shares.genotypes.push_back({{s.name, s.name}, s.share});
            if (auto drawn = sample(shares, rng)) h.strain = drawn->first;
        }
        for (const auto& [locus, g] : genes) h.genotype[locus] = draw(g, sex, genotype_table(locus, breed_id, h.strain), rng);
        return h;
    }

    // The outcome at every locus in a genotype, with interactions between loci applied.
    std::map<std::string, std::optional<Outcome>> outcomes(const Genotype& genotype) const {
        std::map<std::string, std::optional<Outcome>> out;
        for (const auto& [locus, p] : genotype)
            if (const Gene* g = gene(locus)) out[locus] = g->outcome(p.first, p.second, genotype);
        return out;
    }

private:
    static Pair draw(const Gene& g, const std::string& sex, const GenotypeTable* table, const Rng& rng) {
        const Allele* placeholder = g.placeholder();
        std::optional<Pair> p;
        if (table && placeholder) {
            bool male = sex == STALLION;
            GenotypeTable rows{table->locus, table->population, table->evidence, {}};
            for (const auto& s : table->genotypes) {
                bool has = s.alleles.first == placeholder->id || s.alleles.second == placeholder->id;
                if (has == male) rows.genotypes.push_back(s);
            }
            p = sample(rows, rng);
        } else if (table) {
            p = sample(*table, rng);
        }
        if (p) return *p;
        const Allele* wild = g.wild_type();
        if (!wild) throw std::runtime_error(g.id + ": no wild-type allele");
        if (placeholder && sex == STALLION) return {wild->id, placeholder->id};
        return {wild->id, wild->id};
    }
};

}  // namespace oeg

#endif
