# TierManager notes

## Why two folders?
We do not have real CXL memory. So fast_memory/ stands for the fast tier and slow_memory/ stands for the slow tier. This is a simulation.

## Why does migrate() refuse to overwrite?
If a unit already exists in the destination, overwriting it would silently lose data. So migrate() returns an error instead.

## Why are unit ids validated?
Unit ids become file names. Checking them blocks path traversal like ../x.

## Why write the manifest with a temporary file?
The file is written to a .tmp file and then renamed. So tier_manifest.csv is never left half written.
