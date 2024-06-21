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

Other options:
  -s, --show-hashes	print calculated hashes of all files
  -t, --tolerance=NUM	control how similar images must be to be considered
			 'similar'. parameter NUM is an integer from
			 0 (identical) to 64 (very different). defaults to 5
  -h, --help		print this help
```

## dependencies
* ImageMagick 6 or 7 (including libraries)
* gcc
* make
* pkg-config
