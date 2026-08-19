# Image decoder sources and integration decision

## Sources consulted

1. stb repository — https://github.com/nothings/stb
   - `stb_image.h` v2.30 is a single-header image loader supporting JPG and PNG among other formats.
   - It is available under public-domain and MIT alternatives.
   - Upstream notes that the implementation can be configured via compile-time macros and that security issues may take time to be fixed; ATMKoala therefore restricts formats, input size, image dimensions and pixel count.

2. LodePNG repository — https://github.com/lvandeve/lodepng
   - Confirms a dependency-free C/C++ PNG decoder model under the zlib license.
   - Considered but not selected because a single viewer needs both PNG and JPEG rather than a PNG-only decoder.

3. NanoJPEG page — https://keyj.emphy.de/nanojpeg/
   - Documents a compact baseline JPEG-only pure-C decoder, with 8-bit grayscale/YCbCr support and memory requirements roughly proportional to decoded data.
   - Considered as a narrower fallback; not selected because the restricted stb configuration gives one interface for PNG plus common JPEG input.

## ATMKoala integration

Vendored source: `src/stb_image.h` from the first source. The wrapper `image_decode.c` uses memory input only; enables PNG/JPEG only; disables stdio, SIMD, HDR and linear/HDR paths; routes allocation through `kmalloc`/`kfree`; and checks 8 MiB compressed file, 4096 dimension and 2,097,152 pixel limits before decode.

The Image Viewer must clearly report rejected, progressive/unsupported, corrupt, or oversized images rather than attempting unbounded allocation or decoding.
