# Generate Piano Samples

This project generates piano-like sample files from source audio recordings by mapping note names to available samples, filling missing notes with the nearest equivalent note, and pitch-shifting the source audio to the target note.

It is designed around the VCSL sample library in this repository and is useful for creating a complete note set for a keyboard or virtual instrument.

## Setup

Before running the scripts, activate the project virtual environment:

```bash
source .venv/bin/activate
```

On Windows PowerShell, use:

```powershell
.\.venv\Scripts\Activate.ps1
```

The workflow is split into three stages:

1. Build a partial mapping between note names and sample files.
2. Fill any missing notes by using the nearest available sample.
3. Generate output WAV files for all notes by copying or pitch-shifting the original recordings.

The project includes:

- [generate_partial_mapping.py](generate_partial_mapping.py) — scans a folder of WAV files and creates a CSV mapping of note names to sample files.
- [generate_full_mapping.py](generate_full_mapping.py) — fills missing note slots using the nearest available sample.
- [generate_all_samples.py](generate_all_samples.py) — creates output audio files for each note from the full mapping.
- [play_samples.py](play_samples.py) — plays generated files in musical order for quick auditioning.

## Typical workflow

### 1) Build a partial mapping

This script scans a folder of WAV samples and writes a CSV with rows for every note from A0 to A7.

```bash
python generate_partial_mapping.py "VCSL/Chordophones/Composite Chordophones/Concert Harp" "*_f*.wav" concert_harp_notes.csv
```

This creates a CSV with columns:

- `note`
- `sample_note`
- `file_path`

Notes that are not found remain blank so they can be filled later.

### 2) Fill missing notes

The next script uses the nearest valid sample to fill the empty rows:

```bash
python generate_full_mapping.py concert_harp_notes.csv concert_harp_notes_full.csv
```

This produces a complete note mapping such as `concert_harp_notes_full.csv`.

### 3) Generate output WAV files

Now produce the final audio library for all notes:

```bash
python generate_all_samples.py concert_harp_notes_full.csv output_concert_harp
```

`generate_all_samples.py` does the following:

- copies a sample directly when the target note matches the source note
- uses `librosa.effects.pitch_shift()` to shift the sample when the note differs
- trims trailing silence before saving the result
- writes output files in the target folder

### 4) Listen to the generated samples

```bash
python play_samples.py output_concert_harp --duration 1
```

This plays the samples in musical order so you can quickly check the generated note range.

## Project structure

```text
generate_piano_samples/
├── generate_partial_mapping.py
├── generate_full_mapping.py
├── generate_all_samples.py
├── play_samples.py
├── concert_harp_notes.csv
├── concert_harp_notes_full.csv
├── output/
├── output_concert_harp/
├── VCSL/
├── .venv/
└── README.md
```

## Notes on the generation logic

- The code operates on note names such as `A0`, `C4`, `F#3`, and so on.
- The note ordering follows chromatic order starting at C, matching standard musical numbering.
- Missing notes are filled using musically nearest available samples instead of requiring every note to exist in the source library.
- The output filenames are normalized to include the target note and a zero-padded index.

## Dependencies

This project expects Python with the following packages available in the environment:

- `librosa`
- `numpy`
- `soundfile`
- `sounddevice`

A local virtual environment is present in `.venv/`, so it is recommended to use that environment when running the scripts.

## Example command set

```bash
python generate_partial_mapping.py "VCSL/Chordophones/Composite Chordophones/Concert Harp" "*_f*.wav" concert_harp_notes.csv
python generate_full_mapping.py concert_harp_notes.csv concert_harp_notes_full.csv
python generate_all_samples.py concert_harp_notes_full.csv output_concert_harp
python play_samples.py output_concert_harp --duration 1
```

## Output behavior

Generated files are written as mono WAV files in the target output folder. The script prints each generated note and whether it was copied or pitch-shifted.

This makes it straightforward to audit the resulting piano sample set or feed it into another audio pipeline.
