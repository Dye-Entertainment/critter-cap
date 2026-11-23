#!/usr/bin/env python3
"""Synthesizes Small Cap's sound effects and music (all original).

Usage: make_audio.py <resources_dir>
Writes sounds/*.wav (effects) and music/*.flac (loops; needs ffmpeg for FLAC encoding).
"""
import array
import math
import os
import random
import shutil
import subprocess
import sys
import wave

RATE = 22050
rng = random.Random(1234)


# ---------------------------------------------------------------------------------------------
# Basic synthesis
# ---------------------------------------------------------------------------------------------
def silence(seconds):
    return [0.0] * int(seconds * RATE)


def osc(kind, phase, duty=0.5):
    p = phase % 1.0
    if kind == "square":
        return 1.0 if p < duty else -1.0
    if kind == "tri":
        return 4.0 * abs(p - 0.5) - 1.0
    if kind == "saw":
        return 2.0 * p - 1.0
    if kind == "sine":
        return math.sin(2 * math.pi * p)
    return 0.0


def tone(freq, seconds, kind="square", vol=0.5, duty=0.5, attack=0.005, release=0.05,
         freq_end=None, vibrato=0.0, vib_rate=6.0, decay=None):
    n = int(seconds * RATE)
    out = [0.0] * n
    phase = 0.0
    for i in range(n):
        t = i / RATE
        k = i / max(1, n - 1)
        f = freq if freq_end is None else freq * (freq_end / freq) ** k
        if vibrato:
            f *= 1.0 + vibrato * math.sin(2 * math.pi * vib_rate * t)
        phase += f / RATE
        env = min(1.0, t / attack) if attack > 0 else 1.0
        if t > seconds - release:
            env *= max(0.0, (seconds - t) / release)
        if decay:
            env *= math.exp(-t / decay)
        out[i] = osc(kind, phase, duty) * vol * env
    return out


def noise(seconds, vol=0.5, decay=None, lowpass=1.0, highpass=0.0, attack=0.002):
    n = int(seconds * RATE)
    out = [0.0] * n
    lp = 0.0
    prev = 0.0
    for i in range(n):
        t = i / RATE
        v = rng.uniform(-1.0, 1.0)
        lp += (v - lp) * lowpass
        hp = lp - prev * highpass
        prev = lp
        env = min(1.0, t / attack)
        if decay:
            env *= math.exp(-t / decay)
        env *= max(0.0, 1.0 - t / seconds) ** 0.5
        out[i] = hp * vol * env
    return out


def mix(*parts, offsets=None):
    offsets = offsets or [0.0] * len(parts)
    length = max(int(o * RATE) + len(p) for p, o in zip(parts, offsets))
    out = [0.0] * length
    for p, o in zip(parts, offsets):
        start = int(o * RATE)
        for i, v in enumerate(p):
            out[start + i] += v
    return out


def seq(notes, kind="square", vol=0.4, duty=0.5, gap=0.0):
    """notes: list of (freq, seconds)."""
    out = []
    for f, d in notes:
        if f:
            out += tone(f, d - gap, kind, vol, duty, release=min(0.03, d * 0.3))
            out += silence(gap)
        else:
            out += silence(d)
    return out


def write_wav(path, samples, rate=RATE):
    peak = max(0.001, max(abs(s) for s in samples))
    scale = min(1.0, 0.95 / peak)
    data = array.array("h", (int(max(-1.0, min(1.0, s * scale)) * 32000) for s in samples))
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(rate)
        w.writeframes(data.tobytes())


NOTE_INDEX = {"C": 0, "C#": 1, "Db": 1, "D": 2, "D#": 3, "Eb": 3, "E": 4, "F": 5, "F#": 6,
              "Gb": 6, "G": 7, "G#": 8, "Ab": 8, "A": 9, "A#": 10, "Bb": 10, "B": 11}


def note_freq(name):
    letter = name[:-1]
    octave = int(name[-1])
    midi = 12 * (octave + 1) + NOTE_INDEX[letter]
    return 440.0 * 2 ** ((midi - 69) / 12.0)


def N(name):
    return note_freq(name)


# ---------------------------------------------------------------------------------------------
# Sound effects
# ---------------------------------------------------------------------------------------------
def make_sfx(out):
    s = {}
    s["sword"] = mix(noise(0.13, 0.5, decay=0.05, lowpass=0.35),
                     tone(900, 0.1, "saw", 0.12, freq_end=300))
    s["spin"] = mix(noise(0.38, 0.45, lowpass=0.25),
                    tone(300, 0.38, "saw", 0.12, freq_end=1200, vibrato=0.1, vib_rate=14))
    s["charge"] = seq([(N("E6"), 0.05), (N("B6"), 0.05), (N("E7"), 0.12)], "square", 0.25, 0.25)
    s["hit"] = mix(tone(330, 0.09, "square", 0.4, freq_end=110), noise(0.08, 0.35, decay=0.03))
    s["enemy_die"] = mix(noise(0.3, 0.45, decay=0.08, lowpass=0.5),
                         seq([(N("C6"), 0.05), (N("G5"), 0.05), (N("C5"), 0.08)], "square", 0.2))
    s["hurt"] = tone(520, 0.25, "square", 0.4, duty=0.3, freq_end=200, vibrato=0.08, vib_rate=30)
    s["cut"] = noise(0.12, 0.5, decay=0.04, lowpass=0.8, highpass=0.6)
    s["pot"] = mix(noise(0.2, 0.5, decay=0.05, lowpass=0.6),
                   tone(1400, 0.05, "square", 0.15), tone(1900, 0.05, "square", 0.12),
                   offsets=[0, 0.04, 0.09])
    s["gem"] = seq([(N("E6"), 0.06), (N("B6"), 0.14)], "square", 0.28, 0.25)
    s["heart"] = seq([(N("C6"), 0.06), (N("E6"), 0.06), (N("G6"), 0.12)], "tri", 0.45)
    s["key"] = seq([(N("G5"), 0.05), (N("D6"), 0.05), (N("G6"), 0.05), (N("B6"), 0.15)],
                   "square", 0.25, 0.25)
    s["chest"] = mix(tone(110, 0.35, "saw", 0.2, freq_end=180, vibrato=0.05, vib_rate=20),
                     noise(0.3, 0.15, lowpass=0.1))
    # Item fanfare (original).
    s["item"] = mix(
        seq([(N("D5"), 0.12), (N("F#5"), 0.12), (N("A5"), 0.12), (N("D6"), 0.5)], "square", 0.3, 0.5),
        seq([(N("A4"), 0.12), (N("D5"), 0.12), (N("F#5"), 0.12), (N("A5"), 0.5)], "square", 0.18, 0.25),
        seq([(N("D3"), 0.36), (N("D4"), 0.5)], "tri", 0.5))
    s["text"] = tone(1500, 0.022, "square", 0.15, duty=0.25, release=0.01)
    s["door"] = mix(tone(160, 0.2, "square", 0.3, freq_end=90), noise(0.2, 0.2, lowpass=0.15))
    s["unlock"] = mix(tone(1200, 0.04, "square", 0.25), tone(900, 0.05, "square", 0.25),
                      tone(150, 0.25, "square", 0.3, freq_end=80), offsets=[0, 0.06, 0.12])
    s["shutter"] = mix(noise(0.25, 0.5, lowpass=0.2, decay=0.1), tone(90, 0.2, "square", 0.3))
    s["push"] = noise(0.32, 0.4, lowpass=0.06)
    s["switch"] = mix(tone(800, 0.03, "square", 0.3), tone(400, 0.05, "square", 0.3),
                      offsets=[0, 0.03])
    # Puzzle-solved jingle (original).
    s["secret"] = mix(
        seq([(N("A5"), 0.08), (N("C#6"), 0.08), (N("E6"), 0.08), (N("A6"), 0.08),
             (N("G#6"), 0.08), (N("E6"), 0.08), (N("B6"), 0.35)], "square", 0.25, 0.25),
        seq([(N("A3"), 0.32), (N("E4"), 0.32), (N("A4"), 0.4)], "tri", 0.45))
    s["shrink"] = mix(tone(1600, 1.3, "sine", 0.35, freq_end=200, vibrato=0.06, vib_rate=9),
                      seq([(N("E7"), 0.07), (None, 0.1), (N("B6"), 0.07), (None, 0.1),
                           (N("G6"), 0.07), (None, 0.1), (N("E6"), 0.07)], "square", 0.12, 0.25))
    s["grow"] = mix(tone(200, 1.2, "sine", 0.35, freq_end=1600, vibrato=0.06, vib_rate=9),
                    seq([(N("E6"), 0.07), (None, 0.1), (N("G6"), 0.07), (None, 0.1),
                         (N("B6"), 0.07), (None, 0.1), (N("E7"), 0.07)], "square", 0.12, 0.25))
    s["roll"] = noise(0.2, 0.3, lowpass=0.15)
    s["shoot"] = tone(600, 0.08, "square", 0.3, duty=0.25, freq_end=250)
    s["bounce"] = tone(250, 0.14, "tri", 0.5, freq_end=700)
    s["boss_roar"] = mix(tone(80, 0.6, "square", 0.35, freq_end=60, vibrato=0.15, vib_rate=11),
                         noise(0.6, 0.3, lowpass=0.08))
    s["boss_hit"] = mix(tone(200, 0.2, "square", 0.45, freq_end=60), noise(0.2, 0.4, decay=0.06))
    s["boss_die"] = mix(noise(1.3, 0.55, lowpass=0.12, decay=0.6),
                        tone(300, 1.2, "square", 0.25, freq_end=40, vibrato=0.1, vib_rate=8))
    s["thud"] = mix(tone(120, 0.15, "sine", 0.7, freq_end=50), noise(0.1, 0.3, lowpass=0.1))
    s["menu"] = tone(1000, 0.05, "square", 0.2, duty=0.25)
    s["die"] = seq([(N("G5"), 0.15), (N("F5"), 0.15), (N("D5"), 0.15), (N("B4"), 0.15),
                    (N("G4"), 0.45)], "tri", 0.5)
    os.makedirs(out, exist_ok=True)
    for name, samples in s.items():
        write_wav(os.path.join(out, name + ".wav"), samples)
    print("sounds: %d" % len(s))


# ---------------------------------------------------------------------------------------------
# Music
# ---------------------------------------------------------------------------------------------
CHORDS = {
    "C": ["C", "E", "G"], "Dm": ["D", "F", "A"], "Em": ["E", "G", "B"], "F": ["F", "A", "C"],
    "G": ["G", "B", "D"], "Am": ["A", "C", "E"], "E": ["E", "G#", "B"], "Bb": ["Bb", "D", "F"],
    "Gm": ["G", "Bb", "D"], "A": ["A", "C#", "E"], "D": ["D", "F#", "A"], "B": ["B", "D#", "F#"],
    "C7": ["C", "E", "Bb"],
}


def parse_melody(text):
    out = []
    for token in text.split():
        name, length = token.split(":")
        out.append((None if name == "-" else name, int(length)))
    return out


def render_track(song):
    bpm = song["bpm"]
    eighth = 60.0 / bpm / 2.0
    bars = song["chords"]
    total = int(len(bars) * 8 * eighth * RATE)
    buf = [0.0] * (total + RATE)

    def add(samples, at):
        start = int(at * RATE)
        for i, v in enumerate(samples):
            if start + i < len(buf):
                buf[start + i] += v

    # Lead melody.
    t = 0.0
    staccato = song.get("staccato", False)
    for name, length in parse_melody(song["melody"]):
        dur = length * eighth
        if name:
            play = dur * (0.5 if staccato else 0.92)
            add(tone(note_freq(name), play, song.get("lead", "square"), song.get("lead_vol", 0.22),
                     duty=song.get("duty", 0.5), release=min(0.06, play * 0.4),
                     vibrato=0.006 if dur > 0.3 else 0.0, vib_rate=5.5), t)
        t += dur
    # Bass and arpeggios follow the chords (bars may hold two chords separated by '/').
    for bar_index, bar in enumerate(bars):
        bar_start = bar_index * 8 * eighth
        parts = bar.split("/")
        span = 8 // len(parts)
        for part_index, chord_name in enumerate(parts):
            chord = CHORDS[chord_name]
            root = chord[0]
            chord_start = bar_start + part_index * span * eighth
            style = song.get("bass", "pulse")
            for step in range(span):
                at = chord_start + step * eighth
                if style == "pulse" and step % 2 == 0:
                    n = root + ("2" if step % 4 == 0 else "3")
                    add(tone(note_freq(n), eighth * 1.8, "tri", 0.5, release=0.04), at)
                elif style == "drive":
                    n = root + ("2" if step % 2 == 0 else "3")
                    add(tone(note_freq(n), eighth * 0.9, "tri", 0.55, release=0.03), at)
                elif style == "slow" and step == 0:
                    add(tone(note_freq(root + "2"), eighth * span * 0.95, "tri", 0.55,
                             release=0.2), at)
            arp_vol = song.get("arp_vol", 0.07)
            if arp_vol > 0:
                for step in range(span * 2):
                    n = chord[step % 3] + str(song.get("arp_octave", 4))
                    add(tone(note_freq(n), eighth * 0.45, "square", arp_vol, duty=0.125,
                             release=0.02), chord_start + step * eighth * 0.5)
    # Drums.
    drums = song.get("drums", "none")
    if drums != "none":
        for bar_index in range(len(bars)):
            for step in range(8):
                at = (bar_index * 8 + step) * eighth
                if step in (0, 4) and drums in ("rock", "march"):
                    add(tone(120, 0.12, "sine", 0.6, freq_end=45), at)
                if step in (2, 6) and drums in ("rock",):
                    add(noise(0.1, 0.25, lowpass=0.5, decay=0.04), at)
                if drums in ("rock", "march", "light"):
                    add(noise(0.03, 0.08 if drums != "light" else 0.06, lowpass=0.9, highpass=0.8),
                        at)
                if drums == "march" and step in (3, 7):
                    add(noise(0.06, 0.15, lowpass=0.6, decay=0.03), at)
    return buf[:total]


SONGS = {
    "title": dict(bpm=100, bass="pulse", drums="light", arp_octave=4, chords=[
        "G", "Em", "C", "D", "G", "Em", "C/D", "G"],
        melody="B4:2 D5:2 G5:4 E5:2 G5:2 B5:4 C6:3 B5:1 A5:2 G5:2 F#5:4 A5:4 "
               "G5:2 B5:2 D6:4 E6:3 D6:1 B5:2 G5:2 A5:2 C6:2 B5:2 A5:2 G5:8"),
    "village": dict(bpm=116, bass="pulse", drums="rock", chords=[
        "C", "Am", "F", "G", "C", "Am", "F/G", "C", "F", "C", "Dm", "G", "F", "C", "Dm/G", "C"],
        melody="E5:2 G5:2 C6:3 B5:1 A5:3 G5:1 E5:4 F5:2 A5:2 C6:2 A5:2 G5:6 -:2 "
               "E5:2 G5:2 C6:3 D6:1 E6:3 D6:1 C6:2 A5:2 A5:2 C6:2 B5:2 D6:2 C6:6 -:2 "
               "A5:2 F5:2 A5:2 C6:2 G5:4 E5:4 F5:2 A5:2 D6:2 C6:2 B5:4 G5:2 -:2 "
               "A5:2 C6:2 F6:3 E6:1 E6:2 C6:2 G5:4 F5:2 A5:2 B5:2 D6:2 C6:6 -:2"),
    "woods": dict(bpm=132, bass="drive", drums="march", lead_vol=0.2, duty=0.25, chords=[
        "Am", "G", "F", "E", "Am", "G", "F", "E", "C", "G", "Am", "E", "F", "G", "Am", "Am"],
        melody="A4:2 C5:2 E5:2 A5:2 G5:3 F5:1 E5:2 D5:2 C5:2 F5:2 A5:3 G5:1 G#5:4 E5:4 "
               "A5:2 B5:2 C6:2 A5:2 B5:3 A5:1 G5:2 D5:2 F5:2 A5:2 C6:2 D6:2 B5:4 G#5:4 "
               "E5:2 G5:2 C6:4 D6:2 B5:2 G5:4 C6:2 A5:2 E5:2 A5:2 G#5:2 B5:2 E6:4 "
               "F6:3 E6:1 D6:2 C6:2 D6:3 C6:1 B5:2 G5:2 A5:2 C6:2 B5:2 G#5:2 A5:6 -:2"),
    "tiny": dict(bpm=148, bass="pulse", drums="light", staccato=True, lead="square", duty=0.25,
                 arp_vol=0.05, chords=["F", "Bb", "C", "F", "Dm", "Bb", "C", "C7"] * 2,
                 melody=("F5:1 -:1 A5:1 -:1 C6:1 -:1 A5:1 -:1 D6:1 -:1 Bb5:1 -:1 F5:2 -:2 "
                         "E5:1 -:1 G5:1 -:1 C6:1 -:1 Bb5:1 A5:1 A5:2 F5:2 -:4 "
                         "D5:1 F5:1 A5:1 D6:1 C6:2 A5:2 Bb5:1 -:1 D6:1 -:1 F6:2 D6:2 "
                         "C6:1 Bb5:1 A5:1 G5:1 E5:2 G5:2 C6:4 -:4 ") * 2),
    "dungeon": dict(bpm=92, bass="slow", drums="none", arp_vol=0.09, arp_octave=4, lead="tri",
                    lead_vol=0.35, chords=["Dm", "Bb", "Gm", "A", "Dm", "Bb", "Gm/A", "Dm"] * 2,
                    melody=("D5:4 F5:2 A5:2 Bb5:6 A5:2 G5:4 D5:4 E5:4 C#5:4 "
                            "D5:2 F5:2 A5:2 D6:2 C6:4 Bb5:2 A5:2 G5:2 Bb5:2 A5:2 E5:2 D5:8 "
                            "-:8 -:8 -:4 G5:2 F5:2 E5:4 A4:4 "
                            "D5:2 E5:2 F5:2 A5:2 Bb5:4 A5:2 G5:2 G5:2 F5:2 E5:2 C#5:2 D5:8")),
    "boss": dict(bpm=156, bass="drive", drums="rock", duty=0.25, lead_vol=0.2, chords=[
        "Em", "Em", "C", "D", "Em", "Em", "C", "B"] * 2,
        melody=("E5:1 E5:1 G5:1 E5:1 B5:2 A5:2 G5:1 F#5:1 E5:1 F#5:1 G5:2 B5:2 "
                "C6:2 B5:1 A5:1 G5:2 E5:2 D5:2 F#5:2 A5:2 D6:2 "
                "E6:2 D6:1 B5:1 G5:2 B5:2 E5:1 G5:1 B5:1 E6:1 D6:2 B5:2 "
                "C6:2 E6:2 G6:2 E6:2 D#6:4 B5:4 ") * 2),
    "ending": dict(bpm=92, bass="pulse", drums="light", chords=[
        "C", "G", "Am", "F", "C", "G", "F/G", "C"],
        melody="G5:2 E5:2 C6:4 D6:2 B5:2 G5:4 C6:2 A5:2 E5:4 F5:2 A5:2 C6:4 "
               "E6:3 D6:1 C6:2 G5:2 B5:2 D6:2 G6:4 A5:2 C6:2 B5:2 D6:2 C6:8"),
}


def make_music(out):
    os.makedirs(out, exist_ok=True)
    ffmpeg = shutil.which("ffmpeg")
    for name, song in SONGS.items():
        samples = render_track(song)
        wav_path = os.path.join(out, name + ".wav")
        write_wav(wav_path, samples)
        if ffmpeg:
            flac_path = os.path.join(out, name + ".flac")
            subprocess.run([ffmpeg, "-loglevel", "error", "-y", "-i", wav_path,
                            "-compression_level", "8", flac_path], check=True)
            os.remove(wav_path)
        print("music %s: %.1fs" % (name, len(samples) / RATE))


def main():
    root = sys.argv[1] if len(sys.argv) > 1 else "resources"
    make_sfx(os.path.join(root, "sounds"))
    make_music(os.path.join(root, "music"))


if __name__ == "__main__":
    main()
