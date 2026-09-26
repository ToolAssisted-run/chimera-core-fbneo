// The Chimera adapter around FinalBurn Neo (see fbneo-driver.h).
// SPDX-License-Identifier: MIT

#include "fbneo-driver.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cctype>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <initializer_list>
#include <vector>

#include <strings.h>

#include "burnint.h"
#include "state.h"
#include "joyprocess.h"
#include "zip-reader.h"

// ---- what FBNeo expects its frontend to provide ----------------------------

// A recording, always: FBNeo then takes the machine's date from MovieInfo
// instead of the host's clock (the Neo Geo's uPD4990A calendar reads it) and
// seeds its random generator with a constant instead of time(NULL). Those
// are the only two things this switches (burn.cpp).
INT32 is_netgame_or_recording()
{
  return 2;
}

// The machine's date: 2000-01-01 00:00:00, a Saturday - the same fixed epoch
// the other Chimera cores start their clocks at. year is years since 1900,
// month 0-11, as struct tm has them.
struct MovieExtInfo
{
  UINT32 year, month;
  UINT16 day, dayofweek;
  UINT32 hour, minute, second;
};
struct MovieExtInfo MovieInfo = {100, 0, 1, 6, 0, 0, 0};

// the mouse divider FBNeo's input layer owns (nothing here reads a mouse)
INT32 nInputIntfMouseDivider = 1;

// ---- state -----------------------------------------------------------------

static std::string s_error;
static std::string s_machine;
static std::vector<std::shared_ptr<chimera::zip_index>> s_archives;
static bool s_running;

static std::vector<uint32_t> s_video;
static int s_video_w, s_video_h;
static int s_max_w, s_max_h;
static bool s_vertical, s_flipped;
static std::vector<uint32_t> s_draw;   // what FBNeo draws into, before rotation
// Bytes per pixel FBNeo draws at: 4, except CPS-3, whose renderer writes
// 16-bit pixels whatever it is told (cps3run.cpp) - its palette is 5 bits a
// channel, so RGB565 loses nothing, and the pixels are widened afterwards.
static int s_bpp = 4;

static const int kAudioRate = 48000;
static std::vector<int16_t> s_audio;
static int s_audio_frames;

// the project's settings (fbneo_set_option / fbneo_want_dip)
static int s_cpu_clock = 100;
static bool s_force_60hz;
static int s_pcm_interp = 1, s_fm_interp = 0;   // FBNeo's defaults
static int s_socd = 3;                          // FBNeo's default: last input priority, 8-way
static std::vector<std::pair<std::string, std::string>> s_want_dips;
static bool s_reset_first_frame;
static bool s_input_read;   // the last frame read the game's controls (patch 0001)

struct Input
{
  std::string name;
  UINT8 type;
  UINT8* val;
  UINT16* sval;   // an analog input's value
};
static std::vector<Input> s_inputs;
extern "C" {
static void ListDomains();
static void ListSaves();
}

// ---- rom loading -----------------------------------------------------------

// FBNeo asks for its i-th rom. Found by CRC first - rom file names drift
// between set versions, the bytes do not - and then by any of the names the
// driver knows it by. Every archive is searched: a clone's set holds only
// what differs from its parent, and the Neo Geo's bios roms are the bios
// set's.
static INT32 __cdecl LoadRom(UINT8* dest, INT32* wrote, INT32 i)
{
  BurnRomInfo ri{};
  if (BurnDrvGetRomInfo(&ri, i) != 0)
    return 1;
  std::vector<std::string> names;
  for (INT32 aka = 0; aka < 8; aka++)
  {
    char* n = nullptr;
    if (BurnDrvGetRomName(&n, i, aka) != 0 || !n)
      break;
    names.emplace_back(n);
  }
  auto match = [&](const chimera::zip_entry& e, bool by_crc) {
    if (by_crc)
      return ri.nCrc != 0 && e.crc == ri.nCrc && e.size == ri.nLen;
    std::string base = e.path;
    const size_t slash = base.rfind('/');
    if (slash != std::string::npos)
      base = base.substr(slash + 1);
    for (const auto& n : names)
      if (strcasecmp(base.c_str(), n.c_str()) == 0)
        return true;
    return false;
  };
  for (int pass = 0; pass < 2; pass++)
  {
    for (const auto& a : s_archives)
    {
      for (size_t k = 0; k < a->entries.size(); k++)
      {
        if (!match(a->entries[k], pass == 0))
          continue;
        chimera::zip_stream z(a, k);
        const u64 n = z.read_at(0, dest, a->entries[k].size);
        if (wrote)
          *wrote = INT32(n);
        return n == a->entries[k].size ? 0 : 1;
      }
    }
  }
  if (!(ri.nType & BRF_OPT) && !(ri.nType & BRF_NODUMP))
    fprintf(stderr, "[fbneo] rom %s (crc %08x) is in none of the archives given\n",
            names.empty() ? "?" : names[0].c_str(), ri.nCrc);
  return 1;
}

// ---- the picture -----------------------------------------------------------

static UINT32 __cdecl HighCol32(INT32 r, INT32 g, INT32 b, INT32 /*i*/)
{
  return 0xFF000000u | (UINT32(r) << 16) | (UINT32(g) << 8) | UINT32(b);
}

static UINT32 __cdecl HighCol16(INT32 r, INT32 g, INT32 b, INT32 /*i*/)
{
  return (UINT32(r >> 3) << 11) | (UINT32(g >> 2) << 5) | UINT32(b >> 3);
}

static uint32_t Widen565(uint16_t p)
{
  const uint32_t r = (p >> 11) & 31, g = (p >> 5) & 63, b = p & 31;
  return 0xFF000000u | (((r << 3) | (r >> 2)) << 16) | (((g << 2) | (g >> 4)) << 8) |
         ((b << 3) | (b >> 2));
}

// ---- the panel -------------------------------------------------------------

struct PanelShape
{
  const char* machine;
  int players;
  int buttons;      // generic "Button n"; 0 = Neo Geo's A-D and Select
};
static const PanelShape kShapes[] = {
    {"cps1", 4, 6}, {"cps2", 4, 6}, {"cps3", 2, 6}, {"neogeo", 2, 0}, {"system16", 4, 5},
};

static const PanelShape* ShapeOf(const std::string& machine)
{
  for (const auto& sh : kShapes)
    if (machine == sh.machine)
      return &sh;
  return nullptr;
}

static std::vector<std::string> PanelNames(const std::string& machine)
{
  std::vector<std::string> out;
  const PanelShape* sh = ShapeOf(machine);
  if (!sh)
    return out;
  for (int p = 1; p <= sh->players; p++)
  {
    const std::string P = "P" + std::to_string(p) + " ";
    for (const char* d : {"Up", "Down", "Left", "Right"})
      out.push_back(P + d);
    if (sh->buttons)
      for (int b = 1; b <= sh->buttons; b++)
        out.push_back(P + "Button " + std::to_string(b));
    else
      for (const char* b : {"A", "B", "C", "D", "Select"})
        out.push_back(P + b);
    out.push_back(P + "Start");
    out.push_back(P + "Coin");
  }
  for (const char* c : {"Service", "Test", "Reset"})
    out.push_back(c);
  return out;
}

// panel index -> the game's input index (-1: the game has no such control)
static std::vector<int> s_bound;

// The analog controls: two axes per player, "P1 Axis 1", "P1 Axis 2", bound
// by position to the player's analog inputs as the driver lists them (a dial,
// a trackball's X and Y, a paddle). FBNeo's scale: -1024..1023, 0 at rest - a
// relative control (dial, trackball, paddle) takes it as this frame's speed,
// an absolute one as a position.
static const int kAxesPerPlayer = 2;
static std::vector<int> s_axis_bound;

static int AxisCount(const std::string& machine)
{
  for (const auto& sh : kShapes)
    if (machine == sh.machine)
      return sh.players * kAxesPerPlayer;
  return 0;
}

static void BindAxes(const std::string& machine)
{
  s_axis_bound.assign(size_t(AxisCount(machine)), -1);
  for (size_t a = 0; a < s_axis_bound.size(); a++)
  {
    const std::string P = "P" + std::to_string(a / kAxesPerPlayer + 1) + " ";
    int nth = int(a % kAxesPerPlayer);
    for (size_t i = 0; i < s_inputs.size(); i++)
    {
      const auto& in = s_inputs[i];
      if ((in.type & BIT_GROUP_ANALOG) && in.type != BIT_DIPSWITCH && in.sval &&
          in.name.compare(0, P.size(), P) == 0 && nth-- == 0)
      {
        s_axis_bound[a] = int(i);
        break;
      }
    }
  }
}

static bool IsPlayerFixed(const std::string& rest)
{
  for (const char* f : {"Up", "Down", "Left", "Right", "Start", "Coin", "Select"})
    if (rest == f)
      return true;
  return false;
}

// Binds the panel to the game. Directions, Start, Coin and Select by name;
// the buttons by position - a player's digital inputs that are none of those,
// in the order the driver lists them (Street Fighter II's Weak Punch .. Strong
// Kick are Buttons 1-6, Magic Sword's Attack, Jump, Fire 3 Buttons 1-3); the
// cabinet's switches by the few names drivers give them.
static void BindPanel(const std::string& machine)
{
  const std::vector<std::string> names = PanelNames(machine);
  s_bound.assign(names.size(), -1);
  const PanelShape* sh = ShapeOf(machine);
  auto find = [&](const std::string& n) {
    for (size_t i = 0; i < s_inputs.size(); i++)
      if (s_inputs[i].type == BIT_DIGITAL && s_inputs[i].name == n)
        return int(i);
    return -1;
  };
  size_t at = 0;
  for (int p = 1; sh && p <= sh->players; p++)
  {
    const std::string P = "P" + std::to_string(p) + " ";
    std::vector<int> buttons;
    for (size_t i = 0; i < s_inputs.size(); i++)
    {
      const std::string& n = s_inputs[i].name;
      if (s_inputs[i].type == BIT_DIGITAL && n.compare(0, P.size(), P) == 0 &&
          !IsPlayerFixed(n.substr(P.size())))
        buttons.push_back(int(i));
    }
    for (const char* d : {"Up", "Down", "Left", "Right"})
      s_bound[at++] = find(P + d);
    const int nb = sh->buttons ? sh->buttons : 4;
    for (int b = 0; b < nb; b++)
      s_bound[at++] = b < int(buttons.size()) ? buttons[size_t(b)] : -1;
    if (!sh->buttons)
      s_bound[at++] = find(P + "Select");
    s_bound[at++] = find(P + "Start");
    s_bound[at++] = find(P + "Coin");
  }
  auto first = [&](std::initializer_list<const char*> ns) {
    for (const char* n : ns)
      if (int i = find(n); i >= 0)
        return i;
    return -1;
  };
  s_bound[at++] = first({"Service", "Service 1"});
  s_bound[at++] = first({"Test", "Diagnostic", "Diagnostics", "Service Mode"});
  s_bound[at++] = first({"Reset"});
}

// ---- dip switches ----------------------------------------------------------

// A driver's dip list: a group entry {0, 0xFE (0xFD: hidden), 0, n, "Name"}
// and then its n options, each {input, flags, mask, value, "Option"} and
// taking (flags & 0x0F) entries - the extra ones are conditions on other
// switches. Inputs count from the list's 0xF0 offset entry. The defaults are
// the 0xFF entries, one per switch byte.
struct DipOption
{
  std::string name;
  int input;
  UINT8 mask, value;
};
struct DipGroup
{
  std::string name;
  std::vector<DipOption> options;
  int def = -1;
};
static std::vector<DipGroup> s_dips;
static INT32 s_dip_offset;

static UINT8* DipByte(int input)
{
  const size_t at = size_t(input + s_dip_offset);
  return at < s_inputs.size() ? s_inputs[at].val : nullptr;
}

static void ListDips()
{
  s_dips.clear();
  s_dip_offset = 0;
  BurnDIPInfo bdi{};
  std::vector<BurnDIPInfo> all;
  for (UINT32 i = 0; BurnDrvGetDIPInfo(&bdi, i) == 0; i++)
    all.push_back(bdi);
  std::vector<std::pair<int, UINT8>> defaults;   // input -> default byte
  for (const auto& d : all)
  {
    if (d.nFlags == 0xF0)
      s_dip_offset = d.nInput;
    if (d.nFlags == 0xFF)
      defaults.push_back({d.nInput, d.nSetting});
  }
  for (size_t i = 0; i < all.size(); i++)
  {
    if (all[i].nFlags != 0xFE && all[i].nFlags != 0xFD)
      continue;
    DipGroup g;
    g.name = all[i].szText ? all[i].szText : "";
    size_t j = i + 1;
    for (int n = 0; n < all[i].nSetting && j < all.size(); n++)
    {
      const auto& o = all[j];
      g.options.push_back({o.szText ? o.szText : "", o.nInput, o.nMask, o.nSetting});
      j += std::max<size_t>(1, o.nFlags & 0x0F);
    }
    for (size_t k = 0; k < g.options.size() && g.def < 0; k++)
      for (const auto& d : defaults)
        if (d.first == g.options[k].input && (d.second & g.options[k].mask) == g.options[k].value)
        {
          g.def = int(k);
          break;
        }
    // two options of a group may share a name (CPS-3's "Asia"); a setting's
    // options must not
    for (size_t a = 0; a < g.options.size(); a++)
    {
      int n = 1;
      for (size_t b = 0; b < a; b++)
        if (g.options[b].name == g.options[a].name)
          n++;
      if (n > 1)
        g.options[a].name += " #" + std::to_string(n);
    }
    if (!g.name.empty() && !g.options.empty())
      s_dips.push_back(std::move(g));
    i = j - 1;
  }
  // and two groups may (the Neo Geo's two "Coin chutes"); settings must not
  for (size_t a = 0; a < s_dips.size(); a++)
  {
    int n = 1;
    for (size_t b = 0; b < a; b++)
      if (s_dips[b].name == s_dips[a].name)
        n++;
    if (n > 1)
      s_dips[a].name += " #" + std::to_string(n);
  }
}

// ---- the calls -------------------------------------------------------------

extern "C" {

void fbneo_set_machine(const char* machine)
{
  s_machine = machine ? machine : "";
}

int fbneo_set_option(const char* name, const char* value)
{
  if (!name || !value)
    return 0;
  const std::string n = name, v = value;
  auto interp = [&](int& to) {
    if (v == "none") return to = 0, 1;
    if (v == "2-point") return to = 1, 1;
    if (v == "4-point") return to = 3, 1;
    return 0;
  };
  if (n == "cpu_clock")
  {
    const int pct = atoi(value);
    return pct >= 25 && pct <= 400 ? (s_cpu_clock = pct, 1) : 0;
  }
  if (n == "force_60hz")
    return v == "true" || v == "1" ? (s_force_60hz = true, 1) :
           v == "false" || v == "0" ? (s_force_60hz = false, 1) : 0;
  if (n == "pcm_interpolation")
    return interp(s_pcm_interp);
  if (n == "fm_interpolation")
    return interp(s_fm_interp);
  if (n == "socd")
  {
    // FBNeo's modes, in its own order (devices/joyprocess.h)
    static const char* const kModes[] = {"off",        "neutral",      "last-input-4way",
                                         "last-input-8way", "first-input", "up-priority",
                                         "down-priority"};
    for (int i = 0; i < 7; i++)
      if (v == kModes[i])
        return s_socd = i, 1;
    return 0;
  }
  return 0;
}

void fbneo_want_dip(const char* group, const char* option)
{
  if (group && option && *option)
    s_want_dips.emplace_back(group, option);
}

void fbneo_add_archive(const char* path)
{
  auto idx = std::make_shared<chimera::zip_index>();
  std::string err;
  if (!chimera::zip_open(path, *idx, err))
  {
    fprintf(stderr, "[fbneo] %s: %s\n", path, err.c_str());
    return;
  }
  s_archives.push_back(idx);
}

const char* fbneo_error(void)
{
  return s_error.c_str();
}

// the system a driver belongs to, from its hardware code
static const char* SystemOf(INT32 hw)
{
  switch (hw & 0x7F000000)
  {
  case HARDWARE_PREFIX_CAPCOM: return "cps1";
  case HARDWARE_PREFIX_CPS2: return "cps2";
  case HARDWARE_PREFIX_CPS3: return "cps3";
  case HARDWARE_PREFIX_SNK: return "neogeo";
  case HARDWARE_PREFIX_SEGA: return "system16";
  default: return "other";
  }
}

static const char* DisplayName(const std::string& system)
{
  if (system == "cps1") return "CPS-1";
  if (system == "cps2") return "CPS-2";
  if (system == "cps3") return "CPS-3";
  if (system == "neogeo") return "Neo Geo";
  if (system == "system16") return "System 16";
  return system.c_str();
}

// "roms/SSF2T.zip" -> "ssf2t": the driver a rom set is named after
static std::string DriverNameOf(const char* archive)
{
  std::string base = archive ? archive : "";
  const size_t slash = base.find_last_of("/\\");
  if (slash != std::string::npos)
    base = base.substr(slash + 1);
  const size_t dot = base.rfind('.');
  if (dot != std::string::npos)
    base = base.substr(0, dot);
  for (auto& c : base)
    c = char(tolower(c));
  return base;
}

// Makes the named driver the active one, without starting it
static bool SelectDriver(const std::string& name)
{
  static bool lib;
  if (!lib)
  {
    BurnLibInit();
    lib = true;
  }
  for (UINT32 i = 0; i < nBurnDrvCount; i++)
  {
    nBurnDrvActive = i;
    const char* n = BurnDrvGetTextA(DRV_NAME);
    if (n && name == n)
      return true;
  }
  return false;
}

int fbneo_init(const char* game_archive)
{
  s_error.clear();
  const std::string base = DriverNameOf(game_archive);

  // the game's own archive is always searched first
  fbneo_add_archive(game_archive);
  if (s_archives.empty())
  {
    s_error = "cannot read the game's archive " + std::string(game_archive ? game_archive : "");
    return 0;
  }
  std::rotate(s_archives.begin(), s_archives.end() - 1, s_archives.end());

  if (!SelectDriver(base))
  {
    s_error = "no CPS-1, CPS-2, CPS-3, Neo Geo or System 16 game is called '" + base +
              "' - the rom set must keep FBNeo's name for it (" + base + ".zip)";
    return 0;
  }
  const std::string system = SystemOf(BurnDrvGetHardwareCode());
  if (!s_machine.empty() && system != s_machine)
  {
    s_error = "the project says " + std::string(DisplayName(s_machine)) + ", but " + base +
              " is a " + DisplayName(system) + " game - pick " + DisplayName(system) +
              " in the New Project wizard's System box";
    return 0;
  }

  // the picture: 32 bits, drawn into our own buffer at the driver's size
  BurnDrvGetFullSize(&s_max_w, &s_max_h);
  const INT32 flags = BurnDrvGetFlags();
  s_vertical = (flags & BDF_ORIENTATION_VERTICAL) != 0;
  s_flipped = (flags & BDF_ORIENTATION_FLIPPED) != 0;
  s_draw.assign(size_t(s_max_w) * s_max_h, 0);
  s_video.assign(size_t(s_max_w) * s_max_h, 0);
  s_bpp = system == "cps3" ? 2 : 4;
  nBurnBpp = s_bpp;
  BurnHighCol = s_bpp == 2 ? HighCol16 : HighCol32;
  nBurnLayer = 0xFF;
  nSpriteEnable = 0xFF;

  // the sound: a fixed rate; FBNeo makes nBurnSoundLen samples a frame
  nBurnSoundRate = kAudioRate;
  // the project's board settings, which the driver reads while it starts
  nBurnCPUSpeedAdjust = s_cpu_clock * 256 / 100;
  bForce60Hz = s_force_60hz;
  nInterpolation = s_pcm_interp;
  nFMInterpolation = s_fm_interp;
  for (auto& v : nSocd)
    v = s_socd;

  BurnExtLoadRom = LoadRom;
  if (BurnDrvInit() != 0)
  {
    s_error = "FBNeo could not start " + base +
              " - a rom is missing or wrong (the log names it); a clone needs its parent's set, "
              "a Neo Geo game the bios set";
    return 0;
  }
  s_running = true;
  nBurnSoundLen = (kAudioRate * 100 + nBurnFPS / 2) / nBurnFPS;
  s_audio.assign(size_t(nBurnSoundLen) * 2 + 16, 0);
  BurnDrvGetFullSize(&s_max_w, &s_max_h);

  // the game's inputs, and its dip switches at their defaults
  s_inputs.clear();
  BurnInputInfo bii{};
  for (UINT32 i = 0; BurnDrvGetInputInfo(&bii, i) == 0; i++)
  {
    s_inputs.push_back({bii.szName ? bii.szName : "", bii.nType, bii.pVal, bii.pShortVal});
    if (bii.pVal && bii.nType == BIT_DIGITAL)
      *bii.pVal = 0;
  }
  BurnDIPInfo bdi{};
  INT32 dip_offset = 0;
  for (UINT32 i = 0; BurnDrvGetDIPInfo(&bdi, i) == 0; i++)
    if (bdi.nFlags == 0xF0)
    {
      dip_offset = bdi.nInput;
      break;
    }
  for (UINT32 i = 0; BurnDrvGetDIPInfo(&bdi, i) == 0; i++)
  {
    if (bdi.nFlags != 0xFF)
      continue;
    const size_t at = size_t(bdi.nInput + dip_offset);
    if (at < s_inputs.size() && s_inputs[at].val)
      *s_inputs[at].val = UINT8((*s_inputs[at].val & ~bdi.nMask) | (bdi.nSetting & bdi.nMask));
  }
  BindPanel(system);
  BindAxes(system);
  ListDomains();
  ListDips();
  ListSaves();
  // The project's dip switches. A game reads most of its switches as it
  // runs, but some only while it starts (the Neo Geo takes its bios at
  // reset), so a machine whose switches differ from the driver's defaults is
  // reset on its first frame - through the driver's own Reset input - and
  // one that keeps the defaults boots exactly as FBNeo boots it.
  for (const auto& [group, option] : s_want_dips)
  {
    if (!fbneo_dip_set(group.c_str(), option.c_str()))
    {
      s_error = "the dip switch setting '" + group + "' = '" + option + "' is not one " + base +
                " has (its switches: " + std::to_string(s_dips.size()) + " groups)";
      return 0;
    }
  }
  s_reset_first_frame = false;
  for (int g = 0; g < fbneo_dip_group_count(); g++)
    if (fbneo_dip_current(g) != fbneo_dip_default(g))
      s_reset_first_frame = true;
  return 1;
}

void fbneo_exit(void)
{
  if (s_running)
    BurnDrvExit();
  s_running = false;
}

void fbneo_frame(void)
{
  if (!s_running)
    return;
  // What FBNeo draws is the board's own scan, landscape even for a game whose
  // monitor stood on its side: its FULL size (the visible size a vertical
  // driver declares is the upright picture, width and height swapped). Both
  // follow a driver that changes its size (CPS-3's 496-pixel mode).
  INT32 w = 0, h = 0;
  BurnDrvGetFullSize(&w, &h);
  if (size_t(w) * h > s_draw.size())
  {
    s_draw.assign(size_t(w) * h, 0);
    s_video.assign(size_t(w) * h, 0);
  }
  int reset_input = -1;
  if (s_reset_first_frame)
  {
    reset_input = fbneo_input_index("Reset");
    fbneo_set_input(reset_input, 1);
  }
  pBurnDraw = reinterpret_cast<UINT8*>(s_draw.data());
  nBurnPitch = w * s_bpp;
  pBurnSoundOut = s_audio.data();
  nChimeraInputRead = 0;
  BurnDrvFrame();
  s_input_read = nChimeraInputRead != 0;
  if (s_reset_first_frame)
  {
    fbneo_set_input(reset_input, 0);
    s_reset_first_frame = false;
  }
  s_audio_frames = nBurnSoundLen;
  if (s_bpp == 2)
  {
    // widen in place, from the end backwards: pixel i's two bytes sit in the
    // first half of the buffer, never past where pixel i's four bytes go
    const uint16_t* src = reinterpret_cast<const uint16_t*>(s_draw.data());
    for (size_t i = size_t(w) * h; i-- > 0;)
      s_draw[i] = Widen565(src[i]);
  }

  // A vertical game is drawn lying on its side, as its monitor was mounted;
  // the picture handed out is upright.
  if (!s_vertical)
  {
    std::memcpy(s_video.data(), s_draw.data(), size_t(w) * h * 4);
    if (s_flipped)
      std::reverse(s_video.begin(), s_video.begin() + ptrdiff_t(w) * h);
    s_video_w = w;
    s_video_h = h;
  }
  else
  {
    // drawn w x h, shown h x w, turned a quarter anticlockwise (flipped:
    // clockwise)
    for (int y = 0; y < h; y++)
      for (int x = 0; x < w; x++)
      {
        const uint32_t px = s_draw[size_t(y) * w + x];
        const int ox = s_flipped ? (h - 1 - y) : y;
        const int oy = s_flipped ? x : (w - 1 - x);
        s_video[size_t(oy) * h + ox] = px;
      }
    s_video_w = h;
    s_video_h = w;
  }
}

const uint32_t* fbneo_video(int* w, int* h)
{
  *w = s_video_w;
  *h = s_video_h;
  return s_video.data();
}

int fbneo_display_aspect(int* x, int* y)
{
  if (!s_running)
    return 0;
  INT32 ax = 0, ay = 0;
  BurnDrvGetAspect(&ax, &ay);
  if (ax <= 0 || ay <= 0)
    return 0;
  *x = ax;
  *y = ay;
  return 1;
}

int fbneo_video_max(int* w, int* h)
{
  *w = s_vertical ? s_max_h : s_max_w;
  *h = s_vertical ? s_max_w : s_max_h;
  return 1;
}

const int16_t* fbneo_audio(int* frames)
{
  *frames = s_audio_frames;
  return s_audio.data();
}

int fbneo_audio_rate(void)
{
  return kAudioRate;
}

int fbneo_fps100(void)
{
  return nBurnFPS;
}

int fbneo_input_index(const char* name)
{
  for (size_t i = 0; i < s_inputs.size(); i++)
    if (s_inputs[i].type == BIT_DIGITAL && s_inputs[i].name == name)
      return int(i);
  return -1;
}

void fbneo_set_input(int index, int pressed)
{
  if (index < 0 || size_t(index) >= s_inputs.size() || !s_inputs[size_t(index)].val)
    return;
  *s_inputs[size_t(index)].val = pressed ? 1 : 0;
}

int fbneo_input_count(void)
{
  return int(s_inputs.size());
}

const char* fbneo_input_name(int index)
{
  return index >= 0 && size_t(index) < s_inputs.size() ? s_inputs[size_t(index)].name.c_str() : "";
}

int fbneo_input_type(int index)
{
  return index >= 0 && size_t(index) < s_inputs.size() ? s_inputs[size_t(index)].type : 0;
}

int fbneo_axis_count(const char* machine)
{
  return AxisCount(machine ? machine : "");
}

const char* fbneo_axis_name(int index)
{
  static std::string name;
  name = "P" + std::to_string(index / kAxesPerPlayer + 1) + " Axis " +
         std::to_string(index % kAxesPerPlayer + 1);
  return name.c_str();
}

int fbneo_axis_active(int index)
{
  return index >= 0 && size_t(index) < s_axis_bound.size() && s_axis_bound[size_t(index)] >= 0;
}

void fbneo_axis_set(int index, int value)
{
  if (!fbneo_axis_active(index))
    return;
  if (value < -1024) value = -1024;
  if (value > 1023) value = 1023;
  *s_inputs[size_t(s_axis_bound[size_t(index)])].sval = UINT16(INT16(value));
}

int fbneo_input_was_read(void)
{
  return s_input_read ? 1 : 0;
}

int fbneo_panel_count(const char* machine)
{
  return int(PanelNames(machine ? machine : "").size());
}

const char* fbneo_panel_name(const char* machine, int index)
{
  static std::string name;
  const auto names = PanelNames(machine ? machine : "");
  name = index >= 0 && size_t(index) < names.size() ? names[size_t(index)] : "";
  return name.c_str();
}

int fbneo_panel_active(int index)
{
  return index >= 0 && size_t(index) < s_bound.size() && s_bound[size_t(index)] >= 0;
}

void fbneo_panel_set(int index, int pressed)
{
  if (fbneo_panel_active(index))
    fbneo_set_input(s_bound[size_t(index)], pressed);
}

// a game's switches before it starts: the driver's static list
static std::string s_described;
static bool Describe(const char* archive)
{
  if (s_running)
    return true;   // the list is the running game's
  const std::string name = DriverNameOf(archive);
  if (name != s_described)
  {
    s_dips.clear();
    s_described = name;
    if (!SelectDriver(name))
      return false;
    ListDips();
  }
  return true;
}

int fbneo_game_dip_group_count(const char* archive)
{
  return Describe(archive) ? fbneo_dip_group_count() : 0;
}

const char* fbneo_game_dip_group_name(const char* archive, int g)
{
  return Describe(archive) ? fbneo_dip_group_name(g) : "";
}

int fbneo_dip_group_count(void)
{
  return int(s_dips.size());
}

const char* fbneo_dip_group_name(int g)
{
  return g >= 0 && size_t(g) < s_dips.size() ? s_dips[size_t(g)].name.c_str() : "";
}

int fbneo_dip_option_count(int g)
{
  return g >= 0 && size_t(g) < s_dips.size() ? int(s_dips[size_t(g)].options.size()) : 0;
}

const char* fbneo_dip_option_name(int g, int o)
{
  if (o < 0 || o >= fbneo_dip_option_count(g))
    return "";
  return s_dips[size_t(g)].options[size_t(o)].name.c_str();
}

int fbneo_dip_current(int g)
{
  for (int o = 0; o < fbneo_dip_option_count(g); o++)
  {
    const auto& opt = s_dips[size_t(g)].options[size_t(o)];
    const UINT8* b = DipByte(opt.input);
    if (b && (*b & opt.mask) == opt.value)
      return o;
  }
  return -1;
}

int fbneo_dip_default(int g)
{
  return g >= 0 && size_t(g) < s_dips.size() ? s_dips[size_t(g)].def : -1;
}

int fbneo_dip_set(const char* group, const char* option)
{
  for (auto& g : s_dips)
  {
    if (g.name != group)
      continue;
    for (const auto& o : g.options)
      if (o.name == option)
      {
        UINT8* b = DipByte(o.input);
        if (!b)
          return 0;
        *b = UINT8((*b & ~o.mask) | (o.value & o.mask));
        return 1;
      }
  }
  return 0;
}

const char* fbneo_game_name(void)
{
  return s_running ? BurnDrvGetTextA(DRV_NAME) : "";
}

const char* fbneo_system(void)
{
  return s_running ? SystemOf(BurnDrvGetHardwareCode()) : "";
}

// ---- the machine's memory ---------------------------------------------------

static uint64_t s_hash;
static int64_t s_ram_size;
static uint8_t* s_ram_out;

static INT32 __cdecl HashArea(BurnArea* pba)
{
  const uint8_t* p = static_cast<const uint8_t*>(pba->Data);
  for (UINT32 i = 0; p && i < pba->nLen; i++)
  {
    s_hash ^= p[i];
    s_hash *= 1099511628211ull;
  }
  return 0;
}

static INT32 __cdecl SizeArea(BurnArea* pba)
{
  s_ram_size += pba->nLen;
  return 0;
}

static INT32 __cdecl CopyArea(BurnArea* pba)
{
  if (pba->Data)
    std::memcpy(s_ram_out, pba->Data, pba->nLen);
  else
    std::memset(s_ram_out, 0, pba->nLen);
  s_ram_out += pba->nLen;
  return 0;
}

static void Scan(INT32 (__cdecl *cb)(BurnArea*))
{
  if (!s_running)
    return;
  BurnAcb = cb;
  BurnAreaScan(ACB_MEMORY_RAM | ACB_NVRAM | ACB_READ, nullptr);
}

struct Domain
{
  std::string name;
  uint8_t* ptr;
  int64_t size;
};
static std::vector<Domain> s_domains;

static INT32 __cdecl ListArea(BurnArea* pba)
{
  if (pba->Data && pba->nLen)
    s_domains.push_back({pba->szName ? pba->szName : "RAM", static_cast<uint8_t*>(pba->Data),
                         int64_t(pba->nLen)});
  return 0;
}

static void ListDomains()
{
  s_domains.clear();
  Scan(ListArea);
  // two areas may share a name; the domain list must not
  for (size_t i = 0; i < s_domains.size(); i++)
  {
    int n = 1;
    for (size_t j = 0; j < i; j++)
      if (s_domains[j].name == s_domains[i].name ||
          s_domains[j].name.rfind(s_domains[i].name + " #", 0) == 0)
        n++;
    if (n > 1)
      s_domains[i].name += " #" + std::to_string(n);
  }
}

// ---- save data -------------------------------------------------------------
// What a game keeps across power cycles: its NVRAM, a memory card, an EEPROM
// - each area the driver declares for them, as one file named after the area.
// ACB_EEPROM is the flag FBNeo keeps for EEPROMs outside its savestates (it
// persists them as files of its own, which this core never reads or writes).

struct SaveFile
{
  std::string name;
  uint8_t* ptr;
  int64_t size;
};
static std::vector<SaveFile> s_saves;

static std::string SaveName(const char* area)
{
  std::string n = area ? area : "save";
  for (auto& c : n)
    if (!(isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_'))
      c = '_';
  return n + ".bin";
}

static INT32 __cdecl ListSave(BurnArea* pba)
{
  if (pba->Data && pba->nLen)
    s_saves.push_back({SaveName(pba->szName), static_cast<uint8_t*>(pba->Data), int64_t(pba->nLen)});
  return 0;
}

static void ListSaves()
{
  s_saves.clear();
  if (!s_running)
    return;
  BurnAcb = ListSave;
  BurnAreaScan(ACB_NVRAM | ACB_MEMCARD | ACB_EEPROM | ACB_READ, nullptr);
}

int fbneo_save_count(void)
{
  return int(s_saves.size());
}

const char* fbneo_save_name(int i)
{
  return i >= 0 && i < fbneo_save_count() ? s_saves[size_t(i)].name.c_str() : "";
}

const uint8_t* fbneo_save_data(int i, int64_t* size)
{
  if (i < 0 || i >= fbneo_save_count())
    return nullptr;
  *size = s_saves[size_t(i)].size;
  return s_saves[size_t(i)].ptr;
}

int fbneo_save_load(const char* name, const uint8_t* data, int64_t size)
{
  for (auto& sf : s_saves)
    if (sf.name == name)
    {
      if (sf.size != size)
        return -1;
      std::memcpy(sf.ptr, data, size_t(size));
      return 1;
    }
  return 0;
}

int fbneo_domain_count(void)
{
  if (s_domains.empty())
    ListDomains();
  return int(s_domains.size());
}

const char* fbneo_domain_name(int i)
{
  return i >= 0 && i < fbneo_domain_count() ? s_domains[size_t(i)].name.c_str() : "";
}

uint8_t* fbneo_domain_ptr(int i)
{
  return i >= 0 && i < fbneo_domain_count() ? s_domains[size_t(i)].ptr : nullptr;
}

int64_t fbneo_domain_size(int i)
{
  return i >= 0 && i < fbneo_domain_count() ? s_domains[size_t(i)].size : 0;
}

uint64_t fbneo_ram_hash(void)
{
  s_hash = 1469598103934665603ull;
  Scan(HashArea);
  return s_hash;
}

int64_t fbneo_ram_size(void)
{
  s_ram_size = 0;
  Scan(SizeArea);
  return s_ram_size;
}

void fbneo_ram_copy(uint8_t* out)
{
  s_ram_out = out;
  Scan(CopyArea);
}

}  // extern "C"
