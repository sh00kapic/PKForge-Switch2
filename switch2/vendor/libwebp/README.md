# libwebp decoder

PKForge builds the decoding and animation demuxing sources from libwebp 1.6.0,
including the portable, ARM NEON and x86 SSE2 implementations. The source
archive SHA-256 is `e4ab7009bf0629fd11982d4c2aa83964cf244cffba7347ecd39019a9e38c4564`.
The SDK builds a flat object directory, so upstream `src/utils/utils.c`
is named `webp_utils.c` here to avoid a collision with PKSM-Core.

[Upstream release](https://storage.googleapis.com/downloads.webmproject.org/releases/webp/libwebp-1.6.0.tar.gz)
