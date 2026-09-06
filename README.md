# Eudyptula

Eudyptula is a real-time audio effect plugin (VST3, AU, AUv3, AAX and LV2) that runs your signal through AES-256 in ECB mode and plays back the ciphertext as audio. This effect is an adaptation of an old ECB-powered sampler I made for my Computers, Sound and Music class I took for my masters program at Portland State University. It is built with [JUCE](https://juce.com). Pedal version coming in the future!

## Background
- Electronic codebook mode (ECB) is a block cipher mode of encryption. A block cipher mode of encryption is an algorithm that uses a block cipher (a cipher that operates on fixed-length blocks of the input, commonly something like AES) to encrypt something.
- ECB specifically works on the blocks of data by using the same key to independently encrypt each block, and then concatenating the results together. The diagram below depicts how ECB works: ![diagram](https://i.imgur.com/WoEHdRj.png)
- The key thing about any block cipher is that its output is deterministic given the input so that the output can be reliably decrypted to recover the input.
- Thus, ECB is also deterministic due to using the same block cipher for each block.
- However, this determinism leads to some issues regarding the security of ECB: if the given input has a repeating pattern in it, the ciphertext will also have patterns! This is depicted by this famous ECB encryption of Tux, the Linux penguin: ![penguin](https://i.imgur.com/4CzMItx.png)
- This is, obviously, horribly insecure, and is why ECB isn't used in modern cryptography.
- I figured that if ECB works this way on images with patterns in them, it could work with sounds!
- Thus, the cryptographer's chagrin becomes the musician's merriment: I've harnessed ECB to create an interesting audio effect.

## What it does
Eudyptula is a pedal-like effect. Clean audio goes in, Eudyptula does its work, and processed audio comes out. Per block, it:

1. Tracks the pitch of the incoming signal with a YIN-style monophonic tracker (roughly 43 Hz to 1.4 kHz), falling back to a fixed 220 Hz carrier when nothing pitched is detected.
2. Resamples the input onto that carrier, multiplied by the **Harmonic** ratio, so the cipher's block rate is locked to the note being played. This is what keeps the output pitched rather than pure noise.
3. Normalizes each sample against a running envelope and quantizes it to one of **Quantize** levels, then packs it as a 16-bit word. With **Quantize** off, the normalized sample keeps its full 16-bit resolution and the cipher is the only thing coloring it.
4. Runs every 8 samples (16 bytes) through AES-256-ECB. Each passphrase also derives a *key profile*, applied around the cipher itself: a byte-whitening mask, a shuffle of the output sample slots, and three macro axes (subdivision, tone tilt and sparsity), each level-compensated. This is why two keys of the same length give audibly different timbres rather than statistically identical noise.
5. Passes through `8 - Corrupt` samples of the block as plain quantized audio, so **Corrupt** sweeps continuously from clean to fully ciphered.
6. Optionally reuses each ciphered block for **Hold** carrier cycles. Since one block is one cycle of the carrier, repeating it makes the wet waveform genuinely periodic — so high Hold sounds smoother and more tonal, not glitchier.
7. Gates the wet signal below a **Sensitivity**-controlled threshold with a soft knee, so silence stays silent instead of gargling.
8. Mixes wet against dry, then matches the output loudness to the input using a K-weighted running measurement, applies the output **Gain**, and passes the result through a soft limiter.

## Interface

![Eudyptula interface](https://i.imgur.com/XojrMHC.png)

| Control | Range | What it does |
| --- | --- | --- |
| Mode | Encrypt / Decrypt | Which AES direction runs on the block. Decrypt on plaintext sounds different. |
| Mix | 0 – 1 | Dry/wet mix. 0 is fully dry, 1 is fully wet. |
| Harmonic | x0.5 – x8 | Multiplies the tracked pitch to set the carrier, transposing the ciphered layer by octaves. |
| Quantize | Off, 2 – 16 | Number of quantization levels before encryption. Fewer levels means coarser, more brutal ciphertext. **Off** skips quantization altogether, handing the cipher the full 16-bit word for a cleaner, less crushed result. |
| Corrupt | 0 – 8 | How many of the 8 samples per block come from the ciphered bytes. 0 is untouched, 8 is fully ciphered. |
| Hold | 1 – 16 | How many carrier cycles each ciphered block is reused for. At 1, every cycle of the output is a fresh random waveshape, which reads as noise; at 16, the same shape repeats and you get a stable harmonic buzz. Higher is smoother. |
| Sensitivity | 0 – 100 % | Noise gate threshold on the wet path, sweeping from about -20 dB down to -80 dB. |
| Gain | -48 – +24 dB | Output gain, applied after the loudness match. |
| Enc Key / Dec Key | up to 32 chars | The passphrases used for each direction. **Random** fills the field with a random 16-character key. Treat these as timbre selectors and play around with them. |

Keys and all parameters are saved with the host session.

## What it sounds like
It sounds a bit like a bitcrusher, but with a bit more sounds you can get out of it. As well, the quality of the distortion can almost sound like something out of an NES, or a crazy noisy assault. Hold trades noise for tone: low for hiss and grit, high for a clean pitched buzz. Perfect for the Merzbow in us all. Sound samples coming soon!

## Building
JUCE is not included in this repo, so you'll need your own copy. Eudyptula is built against **JUCE 9.0.1**, and uses the `juce_audio_processors_headless` module, so earlier 8.x releases won't work.

1. Clone [JUCE](https://github.com/juce-framework/JUCE) and build the Projucer.
2. Set **File → Global Paths → JUCE modules** to your `JUCE/modules` directory. Every module in this project is set to use the global path, so this is the setting that matters; the per-exporter module paths assume JUCE sits next to the repo (`../JUCE/modules`) and are only consulted if you untick "use global path".
3. Open `Eudyptula.jucer` in the Projucer, save the project to export it, then build the generated project under `Builds/`.

`Builds/` and `JuceLibraryCode/` are generated by the Projucer and are not tracked, so the export step is required on a fresh clone.

## License
- Eudyptula is free software, licensed under the **GNU Affero General Public License v3 or later**. The full text is in [`LICENSE`](LICENSE).
- AGPLv3 is required because Eudyptula builds on JUCE, which is offered under either a paid license or the AGPLv3.
- Third-party components and their licenses are recorded in [`LICENSES.md`](LICENSES.md): [tiny-AES-c](https://github.com/kokke/tiny-AES-c) by kokke (Unlicense) for the AES-256 block cipher, and the K-weighting sample-rate adaptation from the [Klangfreund LUFS Meter](https://github.com/klangfreund/LUFSMeter) by Samuel Gaehwiler (MIT).

## A note on the "encryption"
- **The AES in Eudyptula is a sound-design tool, not a security feature.** ECB was chosen precisely because it is a poor cipher: its determinism is what makes it musically interesting.
- Keys are stored and displayed in plaintext, are used directly as key material with no key stretching, are zero-padded or truncated to 32 bytes, and the audio pipeline is lossy in both directions so nothing can be recovered. Setting Dec Key to match Enc Key does not undo anything.
- Treat the key as a timbre selector. Do not use Eudyptula to conceal audio; it provides no confidentiality, integrity or authentication. See [`LICENSES.md`](LICENSES.md) for details.