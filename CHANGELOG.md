# Changelog

All notable changes to fldigi-ifk-core. Versions follow semantic versioning; the contract tag
`ifk-c1` changes only with an incompatible API change.

## 0.1.0 - 2026-10-17

First release, for wave-owl change `add-fldigi-thor-dominoex-core` (umbrella
`add-fldigi-native-modes`).

- Receiver and transmitter for all 15 THOR and 9 DominoEX submodes of fldigi 4.2.13, ported
  from `thor.cxx`, `thorvaricode.cxx`, `dominoex.cxx` and `dominovar.cxx`: THOR K=7 and K=15
  FEC with soft symbols and soft bits, preamble detection, secondary text of both modes,
  DominoEX's optional MultiPSK-compatible FEC, squelch, fldigi's s/n and metric, an offset
  estimate, fldigi's TX framing, edge shaping and soft start/stop. Capability tag `tx=1`.
- Built on fldigi-dsp-core 0.2.0 (`dsp/`, pinned by `dsp.lock`).
- Goldens from the fldigi 4.2.13 bench: 31 TX overs and 93 RX decodes identical to fldigi
  on the reference platform (also in the sanitizer build); unit tests for loopback,
  secondary text, chunking, offset, limits and noise; CPU benchmark.
