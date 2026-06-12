# Third-Party Notices

KARchipelago incorporates third-party software. Their original copyright
notices and license terms are preserved below as required.

---

## HSDLib

Several files under `scripts/hsd/` contain code ported from HSDLib's C#
implementation (<https://github.com/Ploaj/HSDLib>) - most notably the
archive parser, the reachability walker, the public-symbol classifier,
and the various GX / HSD enum tables used by the explorer and verifier.
Affected files:

- `scripts/hsd/__init__.py`
- `scripts/hsd/archive.py`
- `scripts/hsd/symbols.py`
- `scripts/hsd/walker.py`
- `scripts/hsd/explore.py`
- `scripts/hsd/verify_carved.py`

HSDLib is distributed under the MIT License, reproduced verbatim below.

```
MIT License

Copyright (c) 2021 Ploaj

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

---

## hoshi

The Kirby Air Ride modloader framework `hoshi`
(<https://github.com/UnclePunch/hoshi>) is pulled in as a git submodule
at `externals/hoshi/` and is distributed under the GNU General Public
License v3. See the upstream repository for its canonical `LICENSE` file.

KARchipelago's mod binaries are built against and linked into hoshi at
runtime, which is why the project as a whole is distributed under the
GPLv3 (see [`LICENSE`](LICENSE) at the repository root).
