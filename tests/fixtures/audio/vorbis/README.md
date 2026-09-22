These three 0.2-second Ogg Vorbis tones were synthesized with ffmpeg/libvorbis.
They contain no recording or extracted game audio. Mono is 440 Hz; stereo uses
440 Hz left and 880 Hz right, at the sample rates in the filenames.

`encoder-version.txt` and `fixture-hashes.json` record the encoder and artifacts.
`generate.ps1` reproduces the encoding commands and refuses to overwrite existing
fixtures. Pass a new empty directory to `-OutputDirectory` for regeneration.

The real decoder tests cover memory/file sources, mono/stereo, resampling,
allocation ceilings, negative headers, mixer EOF, replay and sync boundaries.
They do not open a device or prove every malformed Ogg stream is rejected.
