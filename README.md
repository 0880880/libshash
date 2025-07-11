# libshash

**libshash** is a single-header library implementing a Bourne-compatible shell, currently supporting **Windows only** with **zero dependencies**. Linux/Unix support is planned.

## Features

* Single-header: `shash.h`
* Bourne shell compatible
* Lightweight and dependency-free
* Windows-only (for now)

## Planned Commands

The following shell commands are slated for implementation:

* `grep`
* `touch`
* `exit`
* `find`
* `wc`
* `seq`
* `date`
* `export`
* `set`
* `unset`
* `alias`
* `unalias`
* `read`
* `else` `elif`
* `while`, `do`, `done`
* `for`, `in`
* ...

## Usage

Include `shash.h` in your project:

```c
#define SHASH_IMPLEMENTATION
#include "shash.h"
```

## License

MIT
