#pragma once

#include "ofMain.h"

// Every named colour anywhere in visualApp, gathered in one place so any of
// them can be changed by editing a single line here and rebuilding, rather
// than by hunting through whichever screen happens to define it. Nothing
// here decides what a colour *means* - VisualBase and each screen still say
// "the border pulses colorBorderTurquoise", "a shot is colorSalmon" - this
// only holds where each one's actual value lives.
//
// A namespace rather than a class: there is no behaviour here, only named
// constants, and nothing is ever instantiated.
namespace Palette {

// The installation's four canonical hues - Milka-Violett, Flieder, Turkis,
// Lachs. Every other named colour below is a lighter, darker or more/less
// saturated shade of one of these four; nothing in this file introduces a
// fifth base hue. Milka-Violett is the dominant primary that carries the
// shared UI chrome; the other three are lent out per screen as that
// screen's own accent weight - a screen may lean harder on one of them than
// another does, without that being a fifth colour.
inline const ofColor colorPrimaryViolet = ofColor(0x84, 0x29, 0xF9); // Milka-Violett
inline const ofColor colorSecondaryLilac = ofColor(0xC1, 0xA2, 0xFF); // Flieder
inline const ofColor colorAccentTurquoise = ofColor(0x5D, 0xF9, 0xEF); // Turkis
inline const ofColor colorAccentSalmon = ofColor(0xF9, 0xC7, 0xC0); // Lachs

// The shared UI palette: VisualBase's own chrome (the play-area border, the
// "you are here" cursor) and the two Game of Life screens' material
// colours - see each colour's own comment for which is which.
inline const ofColor colorAccent = colorSecondaryLilac; // Flieder - heaped/piled material, background lines, score text
inline const ofColor colorActive = colorPrimaryViolet; // Milka-Violett - ordinary material, the "living" pixel colour
inline const ofColor colorPanelBg = ofColor(53, 16, 100); // dark background - Milka-Violett held back rather than dimmed to grey

// A deep, saturated shade of Lachs - the installation's one warm hue,
// pushed hot enough to read as glow/danger/impact without stepping outside
// the palette. Replaces an earlier orange that was not a shade of any of
// the four hues above at all.
inline const ofColor colorGlow = ofColor(249, 57, 31); // a push/shove, the danger line, muzzle flash, score highlights

// The cursor's own accent, and (InvaderVisual) its shots - see
// VisualBase.h's addHandCursor(). Already a same-hue shade of Lachs (the
// same ~7 degree hue as colorAccentSalmon above, just darker and more
// saturated), so it is left exactly as it was rather than snapped to the
// reference hex.
inline const ofColor colorSalmon = ofColor(255, 145, 130);

// The play-area border's three meanings - resting, "something happened",
// "the mode just changed" - kept as three different hues on purpose, see
// VisualBase.h's own comment on drawPlayAreaBorder().
inline const ofColor colorBorderPurple = ofColor(150, 90, 220);
inline const ofColor colorBorderBase = colorBorderPurple;
inline const ofColor colorBorderTurquoise = ofColor(126, 226, 195);
inline const ofColor colorBorderLilac = ofColor(198, 156, 224);

// Calibration is projected in a maintenance setting, where dense information
// needs stronger contrast than the artwork itself. These colours keep that
// interface aligned with the shared installation palette.
inline const ofColor colorCalibrationBackdrop = ofColor(7, 6, 14, 238);
inline const ofColor colorCalibrationPanel = ofColor(28, 18, 48, 245);
inline const ofColor colorCalibrationPanelAlt = ofColor(44, 31, 70, 245);
inline const ofColor colorCalibrationText = ofColor(250, 248, 255);
inline const ofColor colorCalibrationMuted = ofColor(184, 177, 204);
inline const ofColor colorCalibrationLive = colorBorderTurquoise;
inline const ofColor colorCalibrationWarning = colorGlow;
inline const ofColor colorCalibrationError = colorSalmon;

// InvaderVisual's own additions: a fourth resting colour for the falling
// pieces alongside the shared turquoise/lilac/purple trio, the power-ups'
// single shared hue, and the one moment (a lost round) that must not look
// like the rest of the piece. All three used to be hues outside the four
// above (a green, a blue, a plain red) and are now shades of Lachs and
// Turkis instead.
inline const ofColor colorPillGreen = colorAccentSalmon; // the palette's one warm hue, reading distinct against the cool turquoise/lilac/purple trio
inline const ofColor colorPowerUp = colorAccentTurquoise; // the brightest, most saturated Turkis in use, so a pickup never reads as just another falling piece
inline const ofColor colorAlarm = ofColor(174, 40, 22); // a deeper, more muted shade of the same Lachs hue colorGlow uses, held back so the two still read apart on the same screen

// UniverseVisual's own set, built into its palette vector at setup() and
// then saturation-boosted there (the multiplier stays in UniverseVisual.cpp
// so it is visible as its own step, not baked into the values below).
// Deliberately not the shared violet-forward palette above - see
// UniverseVisual::setup(): "this screen is meant to be the loud one" - but
// still never more than a shade of the same four hues.
inline const ofColor universeTurquoise = colorAccentTurquoise; // the steps
inline const ofColor universeTurquoiseDeep = ofColor(96, 200, 180); // the posts in shadow
inline const ofColor universeTurquoisePale = ofColor(168, 240, 235); // a light but still-saturated Turkis, replacing a near-white "paper" that burned out through the projector
inline const ofColor universeLilac = ofColor(198, 156, 224); // the panels
inline const ofColor universePurple = ofColor(140, 78, 168); // the canopy
inline const ofColor universeSalmon = ofColor(240, 150, 140); // a mid-tone warm shade
inline const ofColor universeAccent = colorAccentSalmon; // the sign's one accent - was an off-palette pink, now the pale reference Lachs itself

}
