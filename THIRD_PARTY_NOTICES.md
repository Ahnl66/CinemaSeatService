# Dependencies — development 0.1.0

Exact source URLs, revisions and SHA-256 hashes are in `dependencies/manifest.json`.

- FFmpeg: LGPL-2.1-or-later; dynamically linked libavcodec and libavutil only. GPL/nonfree, network, avformat and CLI programs disabled. The builder applies one documented fail-closed patch in `dca_xll.c`: return AVERROR_INVALIDDATA before lossy XLL recovery. This can reject otherwise decodable multichannel startup; it never substitutes core audio. Publish this exact builder/patch and source manifest with any binary distribution. Full upstream source and license are retained in the build scratch directory.
- Boost 1.90.0: Boost Software License 1.0, header-only Asio/Beast/UUID use.
- JSON for Modern C++ 3.12.0, Copyright 2013–2025 Niels Lohmann: MIT.

MIT license (JSON for Modern C++):

Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files (the “Software”), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

Project license has not been selected. The user authorized a private source snapshot; no public release or image has been published. Select a project license and complete distribution compliance before publication. Debian runtime image includes its own distribution package copyrights. Base image digest is pinned in docker/Dockerfile; apt build-tool packages are resolved by Debian during build, so this is source-pinned, not a bit-reproducible image claim.
