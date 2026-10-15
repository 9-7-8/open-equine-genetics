# Java reader

One file, `OpenEquineGenetics.java`: Java 17 or newer, no dependencies (it has its own
small JSON parser). Copy it into your source folder, adding a `package` line if you use
packages. The calls are the ones in [../README.md](../README.md); where Python gives
`None`, this reader gives `null`.

## Run the example

From this folder, on Java 22 or newer (the launcher compiles the library for you):

    java Example.java ../..

On Java 17 to 21, compile first:

    javac -d out *.java
    java -cp out Example ../..

`Conformance` prints the report described in ../README.md and runs the same way.

## The main calls

```java
import java.nio.file.Path;

var db = OpenEquineGenetics.Database.load(Path.of("path/to/open-equine-genetics"));
var extension = db.gene("extension");                        // null if no such gene
extension.outcome("E", "e").phenotype().name();              // "Wild type"
for (var foal : extension.cross(new OpenEquineGenetics.Pair("E", "e"),
                                new OpenEquineGenetics.Pair("E", "e")))
    System.out.println(foal.alleles() + " " + foal.probability());
var mare = db.randomHorse("arabian", OpenEquineGenetics.MARE);     // or pass a DoubleSupplier
var outcomes = db.outcomes(mare.genotype());                 // locus id -> Outcome
var pool = db.breed("arabian").pool("extension", null);      // strain may be null
var pair = OpenEquineGenetics.sample(db.genotypeTable("extension", "arabian", null), () -> 0.5);
```

Every type is a record nested in `OpenEquineGenetics` (`Gene`, `Breed`, `Outcome`, `Pair`,
...). Genes and breeds are kept sorted by id; `gene.raw()` and `breed.raw()` hold the whole
file for fields this reader does not model.
