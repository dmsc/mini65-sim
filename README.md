Mini65 - Small 6502 simulator with Atari 8bit bios
==================================================

This is a small 6502 simulator intended to debug command line (console only)
Atari 8-bit programs.

It simulates a subset of the Atari BIOS and includes the Floating Point
package from the Altirra emulator for maximum compatibility.

Currently, the emulated calls are:
- CIO `E:` device, inputs from standard input and outputs to standard output.
- DOS `D:` device, implements file reading and writing to the host
  file-system.
- CIO `K:` device, implements character input from the console.
- RTCLOK is emulated using real time.
- A standard memory map with usable RAM up to 0xBFFF is provided.
- SIO disk device, including booting from an ATR disk image.

The only hardware registers emulated are POKEY `RANDOM` register and GTIA
`CONSOL`.

The simulator support tracing to standard error with optional labels.

