# C11 reference suite

Hand-written C11 programs for compiler validation. Each numbered source isolates a hard C11 feature, and each `_test.c` sibling uses given/when/then blocks, prints `OK`, and exits zero on success.

Optional C11 features are guarded by their `__STDC_NO_*__` macros so mostly-conforming hosts can still build the suite.
