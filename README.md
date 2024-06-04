# imgcomp
Lightweight program for image perpetual hashing using aHash/dHash/pHash.
The hashes are unsigned 64-bit integers.

The main use of this program is to find images that are similar to one another,
and to find duplicates (which can be deleted to save space).
Duplicates are found using a Hamming distance.

## usage
```
Usage: imgcomp [OPTION]... FILES...
Compare similarity of image files.

  Hashing algorithms:
  -a, --ahash		use aHash (average hash)
  -d, --dhash		use dHash [DEFAULT]
  -p, --phash		use pHash (perceptive hash)

  -s, --show-hashes	print calculated hashes of all files
  -h, --help		print this help
```

## dependencies
* ImageMagick 7 (including libraries)
* C99 compiler
