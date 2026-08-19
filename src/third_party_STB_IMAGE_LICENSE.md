# Vendored decoder: stb_image

**Source:** https://github.com/nothings/stb/blob/master/stb_image.h

`stb_image.h` is distributed under public-domain and MIT alternatives by its upstream author. ATMKoala uses a narrowed freestanding configuration: memory decoding only, PNG and JPEG only, no stdio, no SIMD, no HDR, and no image writer.

The integration is bounded by ATMKoala Image Viewer limits before allocation and rendering. It is not a generic image-processing service and does not expose decoder access to untrusted kernel pointers.
