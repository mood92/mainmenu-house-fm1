# Main Menu House (FM-1)

Clipper, cut down to a play-and-recall house sound module: chords, leads and basses only, in the style of late-90s /
early-2000s underground house. Record and arrange in Ableton; the FM-1 is the sound source (USB audio + MIDI).

Built on Clipper (3dSam), SLOOP (isod89) and Felucca (Leo Kuroshita / Hügelton Instruments). GPL-3.0.

## The sounds: MIDI program change (CC 0 bank select: leave at 0)

| Program | Bank | Sounds |
| --- | --- | --- |
| 0-19 | Chords | HOUSE ORGN (the "Show Me Love" organ stab), FULL ORGAN, PERC ORGAN, DIRTY B3, M1 PIANO, DX RHODES, RHODES, WURLI, SUITCASE, DUB CHORD, MIN7 STAB, MIN STAB, MAJ STAB, SUS STAB, RAVE STAB, HOUSE CHRD, CHORD STAB, JUNO 106 PAD, SOLINA, 80S POLY |
| 20-39 | Leads | PAN FLUTE, MINI LEAD, PROPH LEAD, PWM LEAD, WAH LEAD, FAT LEAD, MOOG LEAD, SOFT SYNC, SYNC LEAD, SAW LEAD 106, SYNTHWAVE LD, FM LEAD, CZ LEAD, ACID LEAD, SQR LEAD, AAH LEAD, SYN VIBES, MARIMBA, KALIMBA, SQR PLUCK |
| 40-59 | Basses | TAURUS, MONO BASS, SAW BASS, SUB BASS, MINI BASS, MOOG BASS, DYN BASS, FM BASS, DX BASS, FAT BASS, ACID 303, DEEP 303, SOFT 303, LOW 303, ACID HOUSE, SUB BASS 106, JUNO BASS, ROUND BASS, PLUGG BASS, FUNK BASS |

Version 0.1 curates sounds that already ship in Clipper's engines. Each one gets its own space on load (ui.c
`HOUSE_FX`): the Juno chorus (INSERT JUNO I / II / I+II), delay and reverb sends. The delay is global
(TIME 1/8 dotted by default, GLO > DLY for time and feedback). Default tempo 124 BPM.

Look: charcoal background, lime palette (the first entry of COLOR in the HOME-hold menu).

## Build

`.github/workflows/build.yml` builds `MainMenu-House.fwsc` on GitHub (Actions > build > Run workflow, or any push
to main that touches firmware/). Download it from the run's Artifacts and load it in the FM-1 emulator
(https://joakimeriksson.github.io/fm1-emu/) or flash it with the web installer.
