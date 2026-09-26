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
// A project setting, before fbneo_init: "cpu_clock" (percent), "force_60hz"
// (true/false), "pcm_interpolation" and "fm_interpolation" ("none", "2-point",
// "4-point"), "socd" (how opposite directions held together reach the game).
// 0: no such setting or value.
int fbneo_set_option(const char* name, const char* value);
// A dip switch the project sets, before fbneo_init: applied when the game is
// loaded, by group and option name as fbneo_dip_* list them.
void fbneo_want_dip(const char* group, const char* option);

// Picks the driver the game archive is named after (sf2.zip -> sf2), loads
// it, sets its dip switches to their defaults. 0 on failure; fbneo_error says.
int fbneo_init(const char* game_archive);
const char* fbneo_error(void);
void fbneo_exit(void);

void fbneo_frame(void);
// Did the last frame read the game's controls? No: a lag frame.
int fbneo_input_was_read(void);

// the picture: BGRA, top-down, w x h
const uint32_t* fbneo_video(int* w, int* h);
int fbneo_video_max(int* w, int* h);   // the largest a frame can be
// The picture's display aspect as the driver gives it - of the upright
// picture, so 3:4 for a game whose monitor stood on its side. 0: unknown.
int fbneo_display_aspect(int* x, int* y);
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

// The game's dip switches, grouped as its driver groups them: a group is one
// setting ("Difficulty"), its options the values it can take ("Hard").
int fbneo_dip_group_count(void);
const char* fbneo_dip_group_name(int group);
int fbneo_dip_option_count(int group);
const char* fbneo_dip_option_name(int group, int option);
int fbneo_dip_current(int group);    // which option the switches hold now
int fbneo_dip_default(int group);    // the driver's default option
// The same, for a game that has not started: its driver's static list, by
// the rom set it is named after. fbneo_dip_option_* / fbneo_dip_default then
// answer for it too.
int fbneo_game_dip_group_count(const char* game_archive);
const char* fbneo_game_dip_group_name(const char* game_archive, int group);
// Sets a group to an option by name. 0: no such group or option.
int fbneo_dip_set(const char* group, const char* option);

// The analog axes: per player "P1 Axis 1", "P1 Axis 2", -1024..1023, 0 at
// rest; bound to the player's analog inputs in driver order.
int fbneo_axis_count(const char* machine);
const char* fbneo_axis_name(int index);
int fbneo_axis_active(int index);         // after fbneo_init
void fbneo_axis_set(int index, int value);

// The driver's name and the system it is ("cps2", ...).
const char* fbneo_game_name(void);
const char* fbneo_system(void);

// The machine's memory as domains: each RAM and NVRAM area the driver declares
// (its savestate scan), under the driver's own name for it. Fixed at init.
int fbneo_domain_count(void);
const char* fbneo_domain_name(int i);
uint8_t* fbneo_domain_ptr(int i);
int64_t fbneo_domain_size(int i);

// Save data: the game's NVRAM, memory card and EEPROM areas, one file each,
// named after the area ("NVRAM.bin"). Fixed at init; the bytes are live.
int fbneo_save_count(void);
const char* fbneo_save_name(int i);
const uint8_t* fbneo_save_data(int i, int64_t* size);
// Puts a saved file back, after fbneo_init and before the first frame:
// 1 done, 0 the game has no such area, -1 it has, of another size.
int fbneo_save_load(const char* name, const uint8_t* data, int64_t size);

// FNV-1a over the machine's RAM and NVRAM as the driver declares them
// (BurnAreaScan): what the gate compares between flavors.
uint64_t fbneo_ram_hash(void);
// The same areas laid end to end, for a harness to dump.
int64_t fbneo_ram_size(void);
void fbneo_ram_copy(uint8_t* out);

#ifdef __cplusplus
}
#endif
