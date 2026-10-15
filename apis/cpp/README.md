# C++ reader

One header, `open_equine_genetics.hpp`: C++17, standard library only, no dependencies
(it carries its own small JSON parser). Copy it into your project and `#include` it.
The calls are the ones in [../README.md](../README.md), in namespace `oeg`.

## Build

From this folder, with the repository root two levels up:

```
g++ -std=c++17 -O2 example.cpp -o example && ./example ../..
clang++ -std=c++17 -O2 example.cpp -o example && ./example ../..
cl /std:c++17 /EHsc example.cpp && example ../..
```

`conformance.cpp` builds the same way and prints the report described in
[../README.md](../README.md). g++ 8 also needs `-lstdc++fs`.

## Use

```cpp
#include "open_equine_genetics.hpp"

oeg::Database db = oeg::Database::load("../..");      // throws std::runtime_error naming a bad file
const oeg::Gene* ext = db.gene("extension");          // nullptr if there is no such locus
auto o = ext->outcome("E", "e");                      // std::optional<oeg::Outcome>
o->phenotype->name;                                   // "Wild type"
for (const auto& child : ext->cross({"E", "e"}, {"E", "e"}))
    child.probability;                                // 0.25, 0.5, 0.25
oeg::Horse mare = db.random_horse("arabian", oeg::MARE);
auto coat = db.outcomes(mare.genotype);               // locus -> std::optional<oeg::Outcome>
```

`rng` is any callable returning a double in `[0, 1)`; leave it out to use
`oeg::default_rng()` (std::mt19937). `oeg::default_rng(seed)` repeats a draw. Where a
breed or strain is optional, an empty string means none.
