"""Play wav files from a folder in musical order, e.g. to audit generated piano samples.

Usage:
    python play_samples.py <folder> [--pattern PATTERN] [--duration SECONDS]

Example:
    python play_samples.py concert_harp_output --duration 1
"""

import argparse
import fnmatch
import os
import time

import sounddevice as sd
import soundfile as sf

from generate_partial_mapping import extract_note_from_filename, note_sort_key


def list_samples_sorted(folder, pattern):
    """List (note, file_path) pairs for files in folder matching pattern, sorted in musical order."""
    samples = []
    for file_name in os.listdir(folder):
        if not fnmatch.fnmatchcase(file_name, pattern):
            continue
        note = extract_note_from_filename(file_name)
        if note is not None:
            samples.append((note, os.path.join(folder, file_name)))
    samples.sort(key=lambda row: note_sort_key(row[0]))
    return samples


def play_sample(file_path, duration):
    """Play file_path, optionally truncated to the first duration seconds."""
    data, sr = sf.read(file_path)
    if duration is not None:
        data = data[: int(duration * sr)]
    sd.play(data, sr)
    sd.wait()


def main():
    parser = argparse.ArgumentParser(description="Play wav files from a folder in musical order.")
    parser.add_argument("folder", help="Folder containing the wav files to play.")
    parser.add_argument("--pattern", default="*.wav", help="Glob pattern used to filter file names (default: *.wav).")
    parser.add_argument("--duration", type=float, default=None, help="Play only the first N seconds of each file.")
    parser.add_argument("--pause", type=float, default=0.3, help="Pause in seconds between samples (default: 0.3).")
    args = parser.parse_args()

    samples = list_samples_sorted(args.folder, args.pattern)

    for note, file_path in samples:
        print(f"{note:<4} {file_path}")
        play_sample(file_path, args.duration)
        time.sleep(args.pause)


if __name__ == "__main__":
    main()
