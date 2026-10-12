HOW genes/ WAS PRODUCED ON 2026-10-12
=====================================
These three files are the throwaway tools that made the first export. They are kept so
the export can be repeated or checked, not as part of the data. They are specific to the
mod (Java) and to this one machine's layout; nothing in genes/ depends on them.

OpenDataExport.java     reads the mod's compiled gene registry and writes genes/*.json and
                        mod-map.json. It decides which genes are exported and which are
                        "abstracted" from three lists at its top.
ExportTraitAccess.java  a helper in the mod's "trait" package, needed to read a genotype's
                        health conditions.
science.mjs             copies each gene's wiki Science tab into knowledge/science/, adds
                        the external links to the gene file as citations, and fills
                        "symbol" from a hand-written table at its top.

TO RUN IT AGAIN (from the folder that holds both the mod's folder and this one, in Git
Bash; JDK 25 and Node 22+ on the path)
1. The mod's classes must be compiled already:
   Procedural-Horse-Coats/common/build/classes/java/main. Do NOT run a Gradle build while
   the owner's game is open; ask first.
2. Put the two .java files under a scratch folder in their package paths:
     <scratch>/com/example/horsegenetics/common/genetics/OpenDataExport.java
     <scratch>/com/example/horsegenetics/common/trait/ExportTraitAccess.java
3. CP="Procedural-Horse-Coats/common/build/classes/java/main;Procedural-Horse-Coats/common/build/resources/main"
   javac -cp "$CP" -d <classes> <the two files>
   java -cp "<classes>;$CP" com.example.horsegenetics.common.genetics.OpenDataExport <out>
4. node science.mjs <out> Procedural-Horse-Coats/wiki open-equine-genetics
5. node open-equine-genetics/tools/validate.mjs

WARNING: step 4 OVERWRITES every file in genes/ and knowledge/science/. Any hand
correction made here since the last export is lost. Once the data has been edited by hand,
do not re-run this; follow procedures/re-export-from-the-mod.txt instead.

The planned, permanent version of this exporter is the mod's issue #252.
