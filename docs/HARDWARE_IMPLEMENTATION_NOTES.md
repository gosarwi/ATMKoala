# Hardware Implementation Notes

**Purpose:** Local reference and scope record for ATMKoala hardware work. External material is used only to confirm interfaces and identifiers; no external driver source is imported by this record.

## Intel UHD Graphics 600

Intel's legacy GPU table identifies PCI device ID `0x3185` as **Intel UHD Graphics 600**, **Gen9**, codename **Gemini Lake**.[1] ATMKoala already scans PCI configuration space and has an `i915_fb_t` bridge that receives the bootloader-provided framebuffer. The safe present scope is therefore: detect a matching Intel PCI function, retain the supplied framebuffer, report MMIO/stolen-memory discovery, and avoid taking display ownership or programming modes until GEM/GTT, power wells, display pipe sequencing, panel/EDID, interrupt handling, and memory mapping are designed and tested.

> The current local `i915_fb` component is a framebuffer handoff and diagnostics path, not a complete i915/DRM-compatible modesetting or 3D driver.

## Battery telemetry

The ACPI specification's Control Method Battery model exposes real-time status through `_BST`; `_BIX` extends the static information from `_BIF`.[2] ATMKoala currently has an OEM-specific embedded-controller probe, disabled by default because arbitrary EC I/O can stall unsupported firmware. A real battery implementation should therefore begin with ACPI table discovery and a bounded AML/object evaluation subset for `_BST`/`_BIX`, reporting **unavailable** rather than guessed percentages on unsupported firmware.

## Wi-Fi and Bluetooth

PCI class discovery can establish that a wireless controller exists; it cannot establish association, radio state, or data-plane readiness. Bluetooth commonly requires USB enumeration plus a matching HCI transport. The existing status model must keep device detection, initialized driver state, link/association state, and externally usable networking separate.

## Sources

[1] Intel, “Legacy GPUs,” PCI ID `3185` / Intel UHD Graphics 600 / Gemini Lake: <https://dgpu-docs.intel.com/overview/supported-hardware/legacy-gpus.html>

[2] UEFI Forum, *ACPI Specification*, Power Source and Power Meter Devices: <https://uefi.org/htmlspecs/ACPI_Spec_6_4_html/10_Power_Source_and_Power_Meter_Devices/Power_Source_and_Power_Meter_Devices.html>

## MP3 inspection and HD Audio foundation

RFC 3003 describes `audio/mpeg` as a stream of MPEG audio frames that can be interspersed with non-MPEG metadata; it also notes that such objects are not internally signed or encrypted.[3] The local MP3 module therefore parses a bounded prefix, skips a bounded ID3v2 header, validates a sequence of Layer III frame headers, and exposes metadata only. It does **not** decode psychoacoustic data to PCM.

The frame-header reference used for the local parser documents an 11-bit frame sync, MPEG version/layer fields, bitrate and sample-rate indexes, padding, and channel mode.[4] The implementation was written independently from this format description and its own synthetic frame regression covers only the implemented subset.

Intel HD Audio architecture uses a controller with codec command/response buffers and DMA engines for PCM streaming.[5] ATMKoala now has read-only PCI discovery for the HDA controller but no CORB/RIRB, codec verb, buffer-descriptor, interrupt, stream-DMA, or mixer implementation. Consequently no actual MP3 playback is claimed.

[3] M. Nilsson, RFC 3003, *The audio/mpeg Media Type*: <https://datatracker.ietf.org/doc/html/rfc3003>

[4] MP3' Tech, *MPEG Audio Layer I/II/III frame header*: <http://www.mp3-tech.org/programmer/frame_header.html>

[5] Microsoft, *Intel's HD Audio Architecture*: <https://learn.microsoft.com/en-us/windows-hardware/drivers/audio/intel-s-hd-audio-architecture>
