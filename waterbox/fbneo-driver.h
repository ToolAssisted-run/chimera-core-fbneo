// The Chimera adapter around FinalBurn Neo: pick a game's driver, feed it its
// roms, advance it one frame at a time, and hand out the picture, the sound
// and the machine's memory. Compiled for both flavors; the native reference
// drives it through run-native.cpp, the guest through wbx-entry.cpp.
// SPDX-License-Identifier: MIT
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// which system the project says this is: "cps1", "cps2", "cps3", "neogeo",
// "system16". Empty = no check (a harness).
void fbneo_set_machine(const char* machine);
// A rom archive the game may take roms from: the game's own set, its parent's
// set for a clone, the Neo Geo bios set. Any number, any order.
void fbneo_add_archive(const char* path);
// Picks the driver the game archive is named after (sf2.zip -> sf2), loads
// it, sets its dip switches to their defaults. 0 on failure; fbneo_error says.
int fbneo_init(const char* game_archive);
const char* fbneo_error(void);
void fbneo_exit(void);

void fbneo_frame(void);

// the picture: BGRA, top-down, w x h
const uint32_t* fbneo_video(int* w, int* h);
int fbneo_video_max(int* w, int* h);   // the largest a frame can be
// this frame's sound: interleaved stereo s16 at fbneo_audio_rate()
const int16_t* fbneo_audio(int* frames);
int fbneo_audio_rate(void);
// frames per second x 100 (5997 = 59.97)
int fbneo_fps100(void);

// The game's own inputs, by the name its driver gives them ("P1 Button 1").
// Digital ones only here. -1 = the game has no such input.
int fbneo_input_index(const char* name);
void fbneo_set_input(int index, int pressed);
int fbneo_input_count(void);
const char* fbneo_input_name(int index);   // for the harness's listing
int fbneo_input_type(int index);

// The machine's panel: the controls a project's input roll has, the same list
// for every game of a system. "P1 Up" ... "P1 Button 1".."P1 Button 6" (Neo
// Geo: "P1 A".."P1 D", "P1 Select"), "P1 Start", "P1 Coin", per player, then
// the cabinet's "Service", "Test" and "Reset". Each is bound to the game's own
// input when the game is loaded; a control the game lacks is inactive.
int fbneo_panel_count(const char* machine);
const char* fbneo_panel_name(const char* machine, int index);
int fbneo_panel_active(int index);        // after fbneo_init
void fbneo_panel_set(int index, int pressed);

// The driver's name and the system it is ("cps2", ...).
const char* fbneo_game_name(void);
const char* fbneo_system(void);

// The machine's memory as domains: each RAM and NVRAM area the driver declares
// (its savestate scan), under the driver's own name for it. Fixed at init.
int fbneo_domain_count(void);
const char* fbneo_domain_name(int i);
uint8_t* fbneo_domain_ptr(int i);
int64_t fbneo_domain_size(int i);

// FNV-1a over the machine's RAM and NVRAM as the driver declares them
// (BurnAreaScan): what the gate compares between flavors.
uint64_t fbneo_ram_hash(void);
// The same areas laid end to end, for a harness to dump.
int64_t fbneo_ram_size(void);
void fbneo_ram_copy(uint8_t* out);

#ifdef __cplusplus
}
#endif
