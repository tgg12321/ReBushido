# XA — PlayStation XA-ADPCM streamed audio

Files (and their sizes):

| Path                | Files | Size range          | Role                                  |
|---------------------|------:|---------------------|---------------------------------------|
| `disc/XA_0/MAR_XA00.XA` .. `MAR_XA28.XA` | 29 | 1.2 MB .. 70.7 MB | Voices of the story's cutscenes |
| `disc/XA_1/KAISYA_1.XA`, `KAISYA_2.XA` | 2 | 0.6 MB, 1.0 MB | No known user |
| `disc/XA_1/END_XA1.XA` .. `END_XA3.XA` | 3 | 19.7 MB .. 35.3 MB | Voices of the story's long last scenes |
| `disc/LOADSE/BB2_M1.XA` | 1 | 74.8 MB            | Music tracks 0 to 7                   |
| `disc/LOADSE/BB2_M2.XA` | 1 | 89.1 MB            | Music tracks 8 to 11 and the staff roll's music |
| `disc/LOADSE/BB2_M3.XA` | 1 | 7.7 MB             | The voices of six endings             |

These 37 files are the audio group of the game's file table (`g_cdrom_files`
in `src/main/cdrom.c`, `CDROM_GROUP_AUDIO`), in the order of `enum
CdromAudioFile` (`include/cdrom.h`): `MAR_XA00` to `MAR_XA28` are files 0 to
28, `KAISYA_1` and `KAISYA_2` 29 and 30, `BB2_M1` to `BB2_M3` 31 to 33,
`END_XA1` to `END_XA3` 34 to 36.

These files are PSX CD-XA ADPCM audio streams: 16-bit 4-channel ADPCM
compressed audio, hardware-decoded on the fly by the PSX CD controller.
The standard ([CD-XA spec][1]) is well documented; this doc summarises the
on-disc bytes and notes BB2-specific conventions.

## Sector layout (post-extraction)

The shipped game uses CD-XA Form 2 sectors (2,336 byte payload). When the
disc was extracted by `tools/extract_iso.py` the 24-byte CD-XA subheader
was stripped along with the 12-byte sync and 4-byte sector header, leaving
**2,048 bytes of data per sector** in the extracted `.XA` file. (This is the
same treatment as STR — see `docs/formats/STR.md`.)

A consequence: per-sector channel and coding-info bytes (originally bytes
17–20 of the raw 2,352-byte sector) are not preserved in the extracted file.
The audio data itself is intact and decodable, but the channel-interleave
markers are gone. If you need the original per-sector subheader (for example
to demux a multi-channel XA), you need to re-extract from the BIN/CUE image
with a tool that preserves Form 2 subheaders such as `mkpsxiso` or
`psxavenc`.

## XA-ADPCM stream structure (within each sector)

Each 2,048-byte data payload of a Form 2 sector carries:

```
+0x000  16 x 128-byte "sound groups"
        Each sound group: 16 bytes of headers (4 SHDR) + 112 bytes of ADPCM nibbles
+0x800  -- end --
```

Per sound group:

| Offset | Size | Field                                  |
|--------|------|----------------------------------------|
| 0..3   | 4    | SHDR for sub-block 0 (filter+shift+flags) |
| 4..7   | 4    | SHDR for sub-block 1                   |
| 8..11  | 4    | SHDR for sub-block 2                   |
| 12..15 | 4    | SHDR for sub-block 3                   |
| 16..127| 112  | 28 nibbles × 4 sub-blocks of audio data |

Each sub-block decodes to 28 audio samples; one sound group thus yields 112
samples per channel.

## BB2 file roles

Every file holds several streams (XA channels) side by side; a sound is a
file and a stream of it.

* `MAR_XA00..28.XA` (in `XA_0/`): the voices of the story's cutscenes.
  "MAR" is the studio's "Marionation" engine prefix. The story's scripts
  (`src/main/story.c`) give each cutscene a file and a stream:
  `MAR_XA(n)` is `CDROM_AUDIO_MAR_XA + n`.
* `END_XA1.XA` to `END_XA3.XA` (in `XA_1/`): the voices of the story's long
  last scenes, given the same way (`END_XA(n)` in `story.c`,
  `CDROM_AUDIO_END_XA + n - 1`).
* `KAISYA_1.XA` and `KAISYA_2.XA` (in `XA_1/`): `CDROM_AUDIO_KAISYA_1` and
  `_2` are in the table, and no code names them.
* `BB2_M1.XA` and `BB2_M2.XA` (in `LOADSE/`): the music. A track is a file,
  a stream of it and a length (`CdMusicTrack`, `g_cdrom_music_tracks` in
  `cdrom.c`): tracks 0 to 7 are streams 0 to 7 of `BB2_M1.XA`, tracks 8 to
  11 streams 0 to 3 of `BB2_M2.XA`. Nothing asks for tracks 5 and 6. The
  staff roll's music is `BB2_M2.XA` too.
* `BB2_M3.XA` (in `LOADSE/`): the voices of six endings, streams 0 to 5
  (`g_ending_voices`, `src/main/fight_data.c`).

## Loading at runtime

XA audio is played directly from disc (the runtime never reads XA into
RAM). `cdrom_StartAudio(file_index, xa_channel)` (`cdrom.c`) takes the
file's position from `g_cdrom_files`, not from a filename, sets the stream
with `CdControlB(CdlSetfilter, ...)`, and the CD state machine then starts
it with `CdlReadS`. A file is played from its start, and playing stops
150 sectors before the file's end; a music track (`cdrom_StartMusic`)
stops after its length in sectors, which go by at 150 a second.

The XA file sizes are all sector-aligned:

```
MAR_XA00.XA = 2,260,992  = 1,104 sectors
BB2_M1.XA   = 74,842,112 = 36,544 sectors
BB2_M2.XA   = 89,112,576 = 43,512 sectors
BB2_M3.XA   = 7,651,328  = 3,736 sectors
```

(`MAR_XA00.XA` was hex-dumped — first sector starts with `1A 2C 2C 2C 1A 2C
2C 2C 1C 2C 2C 2C 1C 2C 2C 2C` which is the 16-byte SHDR header of the first
sound group — four 4-byte SHDR entries each containing
`(shift_factor, filter_idx, flags)`.)

## Inspector tool

No dedicated `inspect_xa.py` is provided. Use `tools/inspect_str.py
--hex SECTOR` on the XA file to view raw sector contents; full decoding to
WAV is best handled with [psxavenc][2] or [ffmpeg's xa_adpcm decoder][3].

## Verified by

* File sizes of every BB2 XA file are exact multiples of 2,048 bytes
  (sector-aligned post-extraction).
* `disc/XA_0/MAR_XA00.XA` first 16 bytes form a valid 4-SHDR sound group
  header (`1A 2C 2C 2C ...`).
* `disc/LOADSE/BB2_M1.XA` first 16 bytes form another valid SHDR set
  (`0A 09 0C 0C ...`) confirming the same format.

## References

* [CD-XA ADPCM specification (psx-spx)][1]
* [psxavenc — psx audio/video encoder][2]
* [ffmpeg's `xa_adpcm` decoder][3]

[1]: https://psx-spx.consoledev.net/cdromdrive/#cdrom-xa-audio-adpcm-compression
[2]: https://github.com/WonderfulToolchain/psxavenc
[3]: https://ffmpeg.org/ffmpeg-codecs.html#xa
