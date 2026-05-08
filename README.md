# bgl

[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)
[![C](https://img.shields.io/badge/C-11-blue.svg)](https://en.wikipedia.org/wiki/C11_(C_standard))
[![CI](https://github.com/unidict/bgl/actions/workflows/ci.yml/badge.svg)](https://github.com/unidict/bgl/actions/workflows/ci.yml)

**bgl** — A C library for parsing **Babylon Dictionary (.BGL)** files, extracting dictionary entries, metadata, and embedded resources.

## Features

- Parse BGL dictionary files (gzip-compressed binary format)
- Extract dictionary entries with headwords, definitions, parts-of-speech, and transcriptions
- Support for 62 language codes with automatic charset detection
- Extract embedded resources (images, HTML, etc.)
- Automatic text decoding (charset tags, HTML entities, control characters)
- Iteration over entries and resources
- Cross-platform: Linux, macOS, Windows

## Building

### Prerequisites

- C compiler with C11 support
- zlib
- libxml2
- CMake 3.14+

### Install Dependencies

**macOS:**
```bash
brew install cmake zlib libxml2
```

**Ubuntu/Debian:**
```bash
sudo apt-get install cmake zlib1g-dev libxml2-dev
```

**Fedora/RHEL:**
```bash
sudo dnf install cmake zlib-devel libxml2-devel
```

**Windows:**
Install dependencies using [vcpkg](https://vcpkg.io/):
```cmd
vcpkg install zlib:x64-windows libxml2:x64-windows
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### Build from Source

```bash
git clone https://github.com/unidict/bgl.git
cd bgl

mkdir build && cd build
cmake ..
cmake --build .

# Run tests
ctest --output-on-failure

# Install (optional)
sudo cmake --install .
```

#### CMake Options

| Option | Default | Description |
|--------|---------|-------------|
| `BGL_BUILD_TESTS` | `ON` | Build test suite |
| `BUILD_SHARED_LIBS` | `OFF` | Build shared library instead of static |

## Quick Start

### Reading a Dictionary

```c
#include "bgl_reader.h"
#include <stdio.h>

int main() {
    bgl_reader *reader = bgl_reader_open("dictionary.bgl");
    if (!reader) {
        fprintf(stderr, "Failed to open file\n");
        return 1;
    }

    // Get dictionary info
    const bgl_info *info = bgl_get_info(reader);
    if (info) {
        printf("Title: %s\n", info->title ? info->title : "N/A");
        printf("Author: %s\n", info->author ? info->author : "N/A");
        printf("Entries: %d\n", bgl_get_entry_count(reader));
    }

    // Iterate entries
    bgl_entry_iterator *iter = bgl_entry_iterator_create(reader);
    const bgl_entry *entry;
    int count = 0;
    while ((entry = bgl_entry_iterator_next(iter)) != NULL) {
        printf("%s: %s\n", entry->word, entry->def.body ? entry->def.body : "");
        if (++count >= 10) break;
    }
    bgl_entry_iterator_free(iter);

    bgl_reader_close(reader);
    return 0;
}
```

### Accessing Definition Fields

```c
// Each entry contains a parsed definition with separate fields:
const bgl_entry *entry = bgl_entry_iterator_next(iter);
if (entry) {
    printf("Word: %s\n", entry->word);
    if (entry->def.part_of_speech)
        printf("POS: %s\n", entry->def.part_of_speech);
    if (entry->def.title)
        printf("Title: %s\n", entry->def.title);
    if (entry->def.transcription)
        printf("Transcription: %s\n", entry->def.transcription);
    if (entry->def.body)
        printf("Definition: %s\n", entry->def.body);
    for (int i = 0; i < entry->alternate_count; i++)
        printf("  Alt: %s\n", entry->alternates[i]);
}
```

## Architecture

```
src/
  bgl_reader.h/c    - Main reader, entry/resource iterators
  bgl_definition.h/c - Definition field parser (POS, title, transcription)
  bgl_info.h/c      - Metadata parser (title, author, languages, encoding)
  bgl_language.h/c   - Language (62) and charset (14) lookup tables
  bgl_pos.h/c       - Part-of-speech codes (24 entries)
  bgl_text.h/c      - Text decoding, charset tags, HTML entities
  bgl_util.h        - Byte-order helpers, UTF-8 encoding (header-only)
```

## Platform Support

- **Linux** (tested)
- **macOS** (tested)
- **Windows** (MSVC/MinGW)

## Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch (`git checkout -b feature/amazing-feature`)
3. Commit your changes (`git commit -m 'Add amazing feature'`)
4. Push to the branch (`git push origin feature/amazing-feature`)
5. Open a Pull Request

## License

```
MIT License

Copyright (c) 2026 kejinlu

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Acknowledgments

bgl incorporates the following third-party components:

- **[Unity](https://github.com/ThrowTheSwitch/Unity)** by ThrowTheSwitch (MIT License) — Test framework
- **[zlib](https://zlib.net/)** by Jean-loup Gailly and Mark Adler (zlib License) — Gzip decompression
- **[libxml2](https://gitlab.gnome.org/GNOME/libxml2)** by the GNOME Project (MIT License) — HTML entity decoding

## See Also

- [BGL Binary Format Specification](docs/bgl_format.md)
