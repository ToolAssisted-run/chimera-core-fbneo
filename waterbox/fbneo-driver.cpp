// The Chimera adapter around FinalBurn Neo (see fbneo-driver.h).
// SPDX-License-Identifier: MIT

#include "fbneo-driver.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>
#include <string>
#include <initializer_list>
#include <vector>

#include <strings.h>

#include "burnint.h"
#include "state.h"
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

struct Input
{
  std::string name;
  UINT8 type;
  UINT8* val;
};
static std::vector<Input> s_inputs;
extern "C" {
static void ListDomains();
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

// ---- the calls -------------------------------------------------------------

extern "C" {

void fbneo_set_machine(const char* machine)
{
  s_machine = machine ? machine : "";
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

int fbneo_init(const char* game_archive)
{
  s_error.clear();
  std::string base = game_archive ? game_archive : "";
  const size_t slash = base.find_last_of("/\\");
  if (slash != std::string::npos)
    base = base.substr(slash + 1);
  const size_t dot = base.rfind('.');
  if (dot != std::string::npos)
    base = base.substr(0, dot);
  for (auto& c : base)
    c = char(tolower(c));

  // the game's own archive is always searched first
  fbneo_add_archive(game_archive);
  if (s_archives.empty())
  {
    s_error = "cannot read the game's archive " + std::string(game_archive ? game_archive : "");
    return 0;
  }
  std::rotate(s_archives.begin(), s_archives.end() - 1, s_archives.end());

  BurnLibInit();
  UINT32 found = ~0u;
  for (UINT32 i = 0; i < nBurnDrvCount; i++)
  {
    nBurnDrvActive = i;
    const char* name = BurnDrvGetTextA(DRV_NAME);
    if (name && base == name)
    {
      found = i;
      break;
    }
  }
  if (found == ~0u)
  {
    s_error = "no CPS-1, CPS-2, CPS-3, Neo Geo or System 16 game is called '" + base +
              "' - the rom set must keep FBNeo's name for it (" + base + ".zip)";
    return 0;
  }
  nBurnDrvActive = found;
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
    s_inputs.push_back({bii.szName ? bii.szName : "", bii.nType, bii.pVal});
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
  ListDomains();
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
  INT32 w = 0, h = 0;
  BurnDrvGetVisibleSize(&w, &h);
  if (size_t(w) * h > s_draw.size())
  {
    s_draw.assign(size_t(w) * h, 0);
    s_video.assign(size_t(w) * h, 0);
  }
  pBurnDraw = reinterpret_cast<UINT8*>(s_draw.data());
  nBurnPitch = w * s_bpp;
  pBurnSoundOut = s_audio.data();
  BurnDrvFrame();
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
