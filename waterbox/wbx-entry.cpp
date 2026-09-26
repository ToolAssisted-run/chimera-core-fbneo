/* wbx-entry.cpp - the chimera guest ABI over fbneo-driver.
 *
 * Compiles identically for the guest (miniBox emulibc) and for the native
 * reference build (native-shim/emulibc.h), which is what makes the
 * equivalence gate a real proof: the same driver, the same exports, one in
 * the sandbox and one out of it.
 * SPDX-License-Identifier: MIT
 */
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

#include <emulibc.h>
#include <waterbox_settings.h>
#include <waterbox_slots.h>

#include "fbneo-driver.h"

static char g_loadError[512];
static char g_machine[16];

/* Two channels reach a core: the frontend's PACKED mask in FrameAdvance (every
 * panel here is 64 controls or fewer) and SetButton, which the gate harness
 * drives. A frame is their union, so neither leaves a control stuck down. */
static const int kMaxButtons = 64;
static uint8_t g_setButtons[kMaxButtons];

static bool Exists(const char *name)
{
	FILE *f = fopen(name, "rb");
	if (!f) return false;
	fclose(f);
	return true;
}

/* The rom sets: the Rom set slot's files, the game's own first, then any a
 * clone needs (its parent's). A rom opened with no project has no slot map;
 * the engine mounts it under its own name and says which in rom.name. */
static std::vector<std::string> RomSets()
{
	std::vector<std::string> sets;
	char entry[512];
	const int32_t n = wbx_slot_count("romset");
	for (int32_t i = 0; i < n; i++)
		if (wbx_slot_name("romset", i, entry, sizeof entry) != nullptr)
			sets.emplace_back(entry);
	if (sets.empty())
	{
		FILE *f = fopen("rom.name", "rb");
		if (f)
		{
			size_t got = fread(entry, 1, sizeof entry - 1, f);
			fclose(f);
			entry[got] = '\0';
			sets.emplace_back(entry);
		}
	}
	return sets;
}

static std::string JsonString(const std::string &s)
{
	std::string out = "\"";
	for (char c : s)
	{
		if (c == '"' || c == '\\') { out += '\\'; out += c; }
		else if ((unsigned char)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); out += b; }
		else out += c;
	}
	return out + "\"";
}

/* The game's dip switches as setting declarations, "dip.<group>" each - the
 * Neo Geo's BIOS aside, which is the board setting neogeo_bios. Asked of the
 * driver's static list, so it answers before the game starts too. */
static std::string GameSettingsJson(const char *game)
{
	std::string out = "[";
	bool first = true;
	for (int g = 0; g < fbneo_game_dip_group_count(game); g++)
	{
		const std::string name = fbneo_game_dip_group_name(game, g);
		if (strcmp(g_machine, "neogeo") == 0 && name == "BIOS") continue;
		const int def = fbneo_dip_default(g);
		out += first ? "" : ",";
		first = false;
		out += "{\"name\":" + JsonString("dip." + name) + ",\"display\":" + JsonString(name) +
		       ",\"type\":\"enum\",\"options\":[";
		for (int o = 0; o < fbneo_dip_option_count(g); o++)
			out += (o ? "," : "") + JsonString(fbneo_dip_option_name(g, o));
		out += "],\"default\":" + JsonString(def >= 0 ? fbneo_dip_option_name(g, def) : fbneo_dip_option_name(g, 0));
		out += ",\"description\":" + JsonString("A dip switch of this game's board: '" + name +
		       "'. The game reads it as part of the machine, so a movie needs the same value to play back. "
		       "The default is the driver's.") + "}";
	}
	return out + "]";
}

static std::string g_gameSettings, g_suggestion;

extern "C" {

ECL_EXPORT const char *GetLoadError(void) { return g_loadError; }

/* Called INSTEAD of Init, on the same files: what this game's own settings are,
 * for the new-project wizard (engine.h, ce_suggest_settings). */
ECL_EXPORT const char *SuggestSettings(void)
{
	wbx_setting_str("machine", g_machine, sizeof g_machine);
	const std::vector<std::string> sets = RomSets();
	if (sets.empty())
		return g_suggestion.assign("{\"values\":{},\"note\":\"No rom set yet.\"}").c_str();
	const std::string settings = GameSettingsJson(sets[0].c_str());
	const int groups = fbneo_game_dip_group_count(sets[0].c_str());
	std::string note = groups == 0
		? "This game has no dip switches (a CPS-2 game keeps its settings in its EEPROM, set in its service menu)."
		: "This game's dip switches are below, each at the driver's default.";
	g_suggestion = "{\"values\":{},\"note\":" + JsonString(note) + ",\"settings\":" + settings + "}";
	return g_suggestion.c_str();
}

/* The same declarations, from the running core (ce_session_game_settings) */
ECL_EXPORT const char *GetGameSettings(void)
{
	return g_gameSettings.c_str();
}

ECL_EXPORT int Init(void)
{
	g_loadError[0] = '\0';
	wbx_setting_str("machine", g_machine, sizeof g_machine);
	fbneo_set_machine(g_machine);
	for (const char *key : {"cpu_clock", "force_60hz", "pcm_interpolation", "fm_interpolation", "socd"})
	{
		char val[32];
		if (wbx_setting_str(key, val, sizeof val) > 0 && !fbneo_set_option(key, val))
		{
			snprintf(g_loadError, sizeof g_loadError, "setting %s: unknown value '%s'", key, val);
			return 0;
		}
	}
	/* the Neo Geo's bios is its "BIOS" dip switch, a board setting */
	if (strcmp(g_machine, "neogeo") == 0)
	{
		char bios[96];
		if (wbx_setting_str("neogeo_bios", bios, sizeof bios) > 0)
			fbneo_want_dip("BIOS", bios);
	}

	const std::vector<std::string> sets = RomSets();
	if (sets.empty())
	{
		snprintf(g_loadError, sizeof g_loadError, "no rom set: the project's Rom set slot is empty");
		return 0;
	}
	for (size_t i = 1; i < sets.size(); i++)
		fbneo_add_archive(sets[i].c_str());
	/* The Neo Geo's bios set is project firmware, mounted under its own name */
	if (strcmp(g_machine, "neogeo") == 0)
	{
		if (!Exists("neogeo.zip"))
		{
			snprintf(g_loadError, sizeof g_loadError,
			         "a Neo Geo needs its bios set, neogeo.zip - add it as the project's Neo Geo Bios Set firmware");
			return 0;
		}
		fbneo_add_archive("neogeo.zip");
	}
	g_gameSettings = GameSettingsJson(sets[0].c_str());
	/* The game's own switches: a setting "dip.<group>" per group the project
	 * set (chimera#per-game settings). The groups are the driver's, so they
	 * are asked of the driver's static list before it starts. */
	for (int g = 0; g < fbneo_game_dip_group_count(sets[0].c_str()); g++)
	{
		const std::string group = fbneo_game_dip_group_name(sets[0].c_str(), g);
		char val[128];
		if (wbx_setting_str(("dip." + group).c_str(), val, sizeof val) > 0)
			fbneo_want_dip(group.c_str(), val);
	}
	if (!fbneo_init(sets[0].c_str()))
	{
		snprintf(g_loadError, sizeof g_loadError, "%s", fbneo_error());
		return 0;
	}
	/* Save data the project brought: each file back into the area it came
	 * from, before the machine runs a frame. A file this game has no area
	 * for, or one of the wrong size, is a load error - quietly booting with
	 * a blank NVRAM would be a different machine than the one asked for. */
	const int32_t saves = wbx_slot_count("savedata");
	for (int32_t i = 0; i < saves; i++)
	{
		char name[256];
		if (wbx_slot_name("savedata", i, name, sizeof name) == nullptr)
			continue;
		FILE *f = fopen(name, "rb");
		if (!f) continue;
		std::vector<uint8_t> bytes;
		uint8_t buf[65536];
		size_t got;
		while ((got = fread(buf, 1, sizeof buf, f)) > 0)
			bytes.insert(bytes.end(), buf, buf + got);
		fclose(f);
		const char *base = strrchr(name, '/');
		base = base ? base + 1 : name;
		const int r = fbneo_save_load(base, bytes.data(), (int64_t)bytes.size());
		if (r != 1)
		{
			snprintf(g_loadError, sizeof g_loadError,
			         r == 0 ? "save data %s: %s keeps no such thing" : "save data %s: the wrong size for %s",
			         base, fbneo_game_name());
			return 0;
		}
	}
	return 1;
}

ECL_EXPORT void SetButton(int32_t index, int32_t state)
{
	if (index >= 0 && index < kMaxButtons) g_setButtons[index] = state ? 1 : 0;
}

ECL_EXPORT int IsButtonActive(int32_t index) { return fbneo_panel_active(index); }

/* the panel control's name, for the harness's listing */
ECL_EXPORT const char *GetButtonName(int32_t index) { return fbneo_panel_name(g_machine, index); }
ECL_EXPORT int GetButtonCount(void) { return fbneo_panel_count(g_machine); }

ECL_EXPORT void FrameAdvance(uint64_t packed)
{
	const int count = fbneo_panel_count(g_machine);
	for (int i = 0; i < count && i < kMaxButtons; i++)
		fbneo_panel_set(i, g_setButtons[i] | (int)((packed >> i) & 1));
	fbneo_frame();
}

ECL_EXPORT int IsAxisActive(int32_t index) { return fbneo_axis_active(index); }
ECL_EXPORT void SetAxis(int32_t index, int32_t value) { fbneo_axis_set(index, value); }
ECL_EXPORT int GetAxisCount(void) { return fbneo_axis_count(g_machine); }

ECL_EXPORT int InputWasRead(void) { return fbneo_input_was_read(); }

/* Turbo draws nothing the frontend shows - but FBNeo's drivers still draw:
 * what a driver does in its draw routine can be machine state, so only the
 * copy out (and a vertical game's turn) would be skippable, and that is
 * cheap. Accepted and ignored. */
ECL_EXPORT void SetRenderingEnabled(int) {}

ECL_EXPORT uint32_t *GetVideoBgra(void)
{
	int w, h;
	return const_cast<uint32_t *>(fbneo_video(&w, &h));
}
ECL_EXPORT int GetVideoWidth(void)
{
	int w, h;
	fbneo_video(&w, &h);
	return w;
}
ECL_EXPORT int GetVideoHeight(void)
{
	int w, h;
	fbneo_video(&w, &h);
	return h;
}

/* The display aspect of the picture handed out (Chimera's
 * ce_session_display_aspect): 4:3 for most, 3:4 for a vertical game - which a
 * per-machine virtual size cannot know. */
ECL_EXPORT int GetDisplayAspectX(void)
{
	int x = 0, y = 0;
	return fbneo_display_aspect(&x, &y) ? x : 0;
}
ECL_EXPORT int GetDisplayAspectY(void)
{
	int x = 0, y = 0;
	return fbneo_display_aspect(&x, &y) ? y : 0;
}

ECL_EXPORT int16_t *GetAudio(void)
{
	int n;
	return const_cast<int16_t *>(fbneo_audio(&n));
}
ECL_EXPORT int GetAudioSampleCount(void)
{
	int n;
	fbneo_audio(&n);
	return n;
}

/* the board's own refresh: CPS-1/2 59.63 Hz, CPS-3 59.59, Neo Geo 59.18,
 * System 16 60 */
ECL_EXPORT int GetVsyncNumerator(void) { return fbneo_fps100(); }
ECL_EXPORT int GetVsyncDenominator(void) { return 100; }

/* The game's dip switches as text, one group a line:
 *     dip 'Difficulty' = 'Normal' (default 'Normal') : 'Easy' | 'Normal' | 'Hard'
 * for the harness, and for what a person is told a game's switches are. */
static std::string g_describe;
ECL_EXPORT const char *DescribeDips(void)
{
	g_describe.clear();
	for (int g = 0; g < fbneo_dip_group_count(); g++)
	{
		const int cur = fbneo_dip_current(g), def = fbneo_dip_default(g);
		g_describe += std::string("dip '") + fbneo_dip_group_name(g) + "' = '" +
		              (cur >= 0 ? fbneo_dip_option_name(g, cur) : "?") + "' (default '" +
		              (def >= 0 ? fbneo_dip_option_name(g, def) : "?") + "') :";
		for (int o = 0; o < fbneo_dip_option_count(g); o++)
			g_describe += std::string(o ? " | '" : " '") + fbneo_dip_option_name(g, o) + "'";
		g_describe += "\n";
	}
	return g_describe.c_str();
}

/* save data: the game's NVRAM, memory card and EEPROM, one file each */
ECL_EXPORT int32_t GetSaveDataFileCount(void) { return fbneo_save_count(); }
ECL_EXPORT const char *GetSaveDataFileName(int32_t i) { return fbneo_save_name(i); }
ECL_EXPORT int64_t GetSaveDataFileSize(int32_t i)
{
	int64_t n = 0;
	fbneo_save_data(i, &n);
	return n;
}
ECL_EXPORT const uint8_t *GetSaveDataFileBuffer(int32_t i)
{
	int64_t n = 0;
	return fbneo_save_data(i, &n);
}

/* memory domains: the driver's own RAM and NVRAM areas */
ECL_EXPORT int GetMemoryDomainCount(void) { return fbneo_domain_count(); }
ECL_EXPORT const char *GetMemoryDomainName(int i) { return fbneo_domain_name(i); }
ECL_EXPORT uint8_t *GetMemoryDomainPtr(int i) { return fbneo_domain_ptr(i); }
ECL_EXPORT int64_t GetMemoryDomainSize(int i) { return fbneo_domain_size(i); }
ECL_EXPORT int GetMemoryDomainWritable(int i) { return i >= 0 && i < fbneo_domain_count() ? 1 : 0; }

} /* extern "C" */
