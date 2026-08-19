# TinyGL integration audit

The upstream TinyGL description at <https://bellard.org/TinyGL/> presents TinyGL as a compact software-only subset of OpenGL intended for embedded systems and games. Its primary modules are mathematical routines, OpenGL-like emulation, Z-buffer/rasterisation, and a platform integration layer.

The C99 fork reviewed at <https://github.com/jserv/tinygl> is MIT licensed and uses custom `gl_malloc`/`gl_free` hooks, but its full renderer still depends on 32-bit IEEE floating point and `sin`/`cos`, as well as standard-library headers such as `math.h`, `stdlib.h`, `string.h`, `assert.h` and `stdarg.h`. ATMKoala builds freestanding with `-msoft-float`, no libc and no linked libm. Importing the full upstream renderer unchanged would therefore introduce unsupported runtime dependencies.

ATMKoala consequently uses `TinyGL-Lite`: a local, fixed-point, software-only, OpenGL-inspired subset. It provides an independent VBE-backed z-buffer, triangle rasterisation and a rotating-cube demo, but deliberately does not claim OpenGL 1.1 conformance, upstream TinyGL binary/source compatibility, GPU acceleration, GLSL or Linux `libGL.so` compatibility.
