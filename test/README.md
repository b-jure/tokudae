## Testing
`test_{platform}.toku` is standalone executable script used for running tests.
Every directory in this directory represents a testsuite, and any Tokudae
script file (filenames ending with `.toku` that do not start with `.`) in
the testsuite directory is considered as a test belonging to that testsuite.

This directory also contains special directories which are used by tests and
so they are to be ignored, these directories start with `_`, such as `_libs`
(they are not considered to be a testsuite).

For more information do `./test_{platform}.toku -h` or see `test_{platform}.toku`.
(The `test_windows.toku` must be run as `..\tokudae.exe test_windows.toku`.)
