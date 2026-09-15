# Licences

Eudyptula is released as free and open-source software. This file records the
licence of the project itself and of every third-party component that ships.

---

## ⚠️ The AES here is not a security feature

---

## Eudyptula

Copyright (C) 2026 aabagdi

Licensed under the **GNU Affero General Public License, version 3 or later**
(AGPLv3+). The full text is in [`LICENSE`](LICENSE).

---

## Third-party components

| Component | Used for | Licence |
|---|---|---|
| [JUCE](https://juce.com) | Plugin framework, audio/GUI infrastructure | AGPLv3 or commercial |
| [tiny-AES-c](https://github.com/kokke/tiny-AES-c) | AES-256 ECB block cipher | The Unlicense (public domain) |
| [LUFS Meter](https://klangfreund.com/lufsmeter/) | ITU-R BS.1770 K-weighting sample-rate adaptation | MIT |

---

### JUCE

Copyright (c) Raw Material Software Limited.

JUCE is licensed under either the JUCE End User Licence Agreement or the
AGPLv3, at your option:

- JUCE 9 licence: <https://juce.com/legal/juce-9-licence/>
- AGPLv3: <https://www.gnu.org/licenses/agpl-3.0.en.html>

JUCE is **not** vendored into this repository; it is referenced as an external
dependency via the module paths in `Eudyptula.jucer`.

---

### tiny-AES-c — by kokke

Vendored, with modifications, as `Eudyptula/Source/ThirdParty/tiny_aes.h`.

Released into the public domain under **the Unlicense**. No attribution is
required, but it is given here and in the plugin's UI as a courtesy.

Modifications made for Eudyptula.

Structural:

- `aes.h` and `aes.c` merged into a single header, `tiny_aes.h`.
- `AES_init_ctx`, `AES_ECB_encrypt` and `AES_ECB_decrypt` given internal linkage
  (`static`) so the header can be included without adding a compiled `.c` file
  and without risking duplicate symbols. The internal helpers were already
  `static` upstream.

Feature removal:

- CBC mode removed: `AES_CBC_encrypt_buffer`, `AES_CBC_decrypt_buffer`,
  `XorWithIv`.
- CTR mode removed: `AES_CTR_xcrypt_buffer`.
- IV handling removed: `AES_init_ctx_iv`, `AES_ctx_set_iv`, and the `Iv[]`
  member of `struct AES_ctx`.
- AES-128 and AES-192 support removed; `Nk` and `Nr` are hardcoded to 8 and 14.
- The `MULTIPLY_AS_A_FUNCTION` alternative dropped; only the macro form of
  `Multiply` is kept.

---

### LUFS Meter / SecondOrderIIRFilter — by Samuel Gaehwiler (Klangfreund)

The sample-rate adaptation in `Eudyptula/Source/DSP/KWeightingFilter.h` is
derived from `SecondOrderIIRFilter` in the Klangfreund LUFS Meter. BS.1770
publishes K-weighting coefficients only at 48 kHz: that code back-solves the
filter's `Q / VH / VB / VL` from them and re-derives correct coefficients at any
sample rate.

**The MIT licence requires this notice be retained:**

```
The MIT License (MIT)

Copyright (c) 2018 Klangfreund, Samuel Gaehwiler

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
```

---

## Standards referenced

The K-weighting filter coefficients are taken from **ITU-R BS.1770-4**, Tables 1
and 2. Tthe ITU document itself is subject to ITU copyright and is not redistributed here.
