# imgcomp
Lightweight program for image perceptual hashing using aHash/dHash/pHash.
The hashes are unsigned 64-bit integers.

The main use of this program is to find images that are similar to one another,
and to find duplicates (which can be deleted to save space).
Duplicates are found using a Hamming distance.

Calculated hashes are stored in an SQLite database file in `$HOME/.cache/imgcomp.sqlite` or `$XDG_CACHE_HOME/imgcomp.sqlite` if `$XDG_CACHE_HOME` is defined.  
If an images mtime and filesize are unchanged and there exists a hash for the chosen hashing algorithm, then it will use the precalculated hash.
This saves a significant amount of time for repeated uses of imgcomp.

Only GNU/Linux is supported although *BSD will likely work.

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
* ImageMagick 6 or 7 (including MagickWand)
* sqlite3
* gcc
* make
* pkg-config
