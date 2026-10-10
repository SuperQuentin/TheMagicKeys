"""Display and build a csv file with the partial mapping between piano notes (A0..A7) and sample wav files found in a folder.

Usage:
    python generate_partial_mapping.py <folder> <pattern> <output_csv>

Example:
    python generate_partial_mapping.py "VCSL/Chordophones/Composite Chordophones/Concert Harp" "*_f*.wav" concert_harp_notes.csv
"""

import argparse
import csv
import fnmatch
import os
import re
import sys

# Chromatic note order starting at C, matching standard octave numbering (octave increments at C).
NOTE_ORDER = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]

NOTE_PATTERN = re.compile(r"^([A-G]#?)(\d+)$")


def note_sort_key(note_name):
    """Return an integer sort key for a note name such as 'A0' or 'F#3'."""
    match = NOTE_PATTERN.match(note_name)
    if not match:
        raise ValueError(f"Invalid note name: {note_name!r}")
    letter, octave = match.group(1), int(match.group(2))
    return octave * 12 + NOTE_ORDER.index(letter)


def extract_note_from_filename(file_name):
    """Extract the note name found between underscores in a file name, or None if not found."""
    base_name = os.path.splitext(file_name)[0]
    for part in base_name.split("_"):
        if NOTE_PATTERN.match(part):
            return part
    return None


def build_all_notes(first_note="A0", last_note="A7"):
    """Build the list of all notes between first_note and last_note (inclusive), in musical order."""
    first_key = note_sort_key(first_note)
    last_key = note_sort_key(last_note)
    all_notes = []
    for key in range(first_key, last_key + 1):
        octave, index = divmod(key, 12)
        all_notes.append(f"{NOTE_ORDER[index]}{octave}")
    return all_notes


def build_sample_list(file_paths_filtered):
    """Build the list of (sample_note, file_path) pairs, sorted in musical order."""
    sample_list = []
    for file_path in file_paths_filtered:
        note = extract_note_from_filename(os.path.basename(file_path))
        if note is not None:
            sample_list.append((note, file_path))
    sample_list.sort(key=lambda row: note_sort_key(row[0]))
    return sample_list


def build_all_notes_mapping(all_notes, sample_list):
    """Build the list of (note, sample_note, file_name) by matching all_notes against sample_list."""
    samples_by_note = {sample_note: file_name for sample_note, file_name in sample_list}
    all_notes_mapping = []
    for note in all_notes:
        if note in samples_by_note:
            all_notes_mapping.append((note, note, samples_by_note[note]))
        else:
            all_notes_mapping.append((note, "", ""))
    return all_notes_mapping


def display_all_notes_mapping(all_notes_mapping):
    print(f"{'note':<4} {'sample_note':<11} {'file_path'}")
    for note, sample_note, file_path in all_notes_mapping:
        print(f"{note:<4} {sample_note:<11} {file_path}")


def write_all_notes_mapping_csv(all_notes_mapping, output_csv):
    with open(output_csv, "w", newline="", encoding="utf-8") as csv_file:
        writer = csv.writer(csv_file)
        writer.writerow(["note", "sample_note", "file_path"])
        writer.writerows(all_notes_mapping)


def main():
    parser = argparse.ArgumentParser(description="Display the mapping between piano notes and sample wav files.")
    parser.add_argument("folder", help="Folder containing sample wav files.")
    parser.add_argument("pattern", help="Glob pattern used to filter file names, e.g. '*_f*.wav'.")
    parser.add_argument("output_csv", help="Path of the CSV output file to write.")
    args = parser.parse_args()

    if not os.path.isdir(args.folder):
        print(f"Error: folder not found: {args.folder}", file=sys.stderr)
        sys.exit(1)

    file_names = os.listdir(args.folder)
    # Use fnmatchcase: fnmatch() is case-insensitive on Windows, which would wrongly
    # match note names (e.g. "_F2_") against a velocity pattern like "*_f*.wav".
    file_names_filtered = [f for f in file_names if fnmatch.fnmatchcase(f, args.pattern)]
    file_paths_filtered = [os.path.join(args.folder, f) for f in file_names_filtered]

    sample_list = build_sample_list(file_paths_filtered)
    all_notes = build_all_notes("A0", "A7")
    all_notes_mapping = build_all_notes_mapping(all_notes, sample_list)

    display_all_notes_mapping(all_notes_mapping)
    write_all_notes_mapping_csv(all_notes_mapping, args.output_csv)


if __name__ == "__main__":
    main()
