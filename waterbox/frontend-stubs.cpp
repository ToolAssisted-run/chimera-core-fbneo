// What FBNeo's library expects a frontend to provide, and this core's answer
// to each. Nothing here emulates anything: they are the frontend's own
// features (IPS patches, rom-data files, the Neo Geo CD drive, window
// re-initialisation, file paths) that a headless arcade core does not have.
// SPDX-License-Identifier: MIT
#include <cstring>

#include "burnint.h"
#include "cd_interface.h"

// rom-data files (FBNeo's own "romdata" feature): none
RomDataInfo* pRDI = nullptr;
BurnRomInfo* pDataRomDesc = nullptr;

// IPS patches to a rom set: never applied
UINT32 nIpsDrvDefine = 0, nIpsMemExpLen[SND2_ROM + 1] = {};
bool bDoIpsPatch = false;
void IpsApplyPatches(UINT8*, char*, UINT32, bool) {}

// The folders a frontend keeps EEPROMs, high scores, samples and blend tables
// in. A directory that does not exist, in both flavors: nothing is read from
// the host or written to it, so an EEPROM starts as the driver's default and
// what a game saves lives in the machine (and in its savestates).
TCHAR szAppHiscorePath[MAX_PATH] = "/chimera-none/";
TCHAR szAppSamplesPath[MAX_PATH] = "/chimera-none/";
TCHAR szAppBlendPath[MAX_PATH] = "/chimera-none/";
TCHAR szAppEEPROMPath[MAX_PATH] = "/chimera-none/";

int bDrvOkay = 0;

// A driver that changes its picture's size asks the frontend to rebuild its
// window. The driver here reads the visible size every frame instead.
void Reinitialise() {}
void ReinitialiseVideo() {}

char* TCHARToANSI(const TCHAR* in, char* out, int len)
{
  static char buf[1024];
  char* dst = out ? out : buf;
  const int n = out ? len : int(sizeof buf);
  std::strncpy(dst, in ? in : "", size_t(n) - 1);
  dst[n - 1] = '\0';
  return dst;
}

// one file out of an archive by name (high-score tables, blend tables): none
INT32 __cdecl ZipLoadOneFile(char*, const char*, void**, INT32*)
{
  return 1;
}

// The Neo Geo driver carries the Neo Geo CD's code too; this core has no CD.
void NeoCDInfo_Exit() {}
CDEmuStatusValue CDEmuStatus = idle;
INT32 CDEmuStop() { return 0; }
INT32 CDEmuPlay(UINT8, UINT8, UINT8) { return 1; }
INT32 CDEmuLoadSector(INT32, char*) { return 0; }
UINT8* CDEmuReadTOC(INT32) { return nullptr; }
UINT8* CDEmuReadQChannel() { return nullptr; }
INT32 CDEmuGetSoundBuffer(INT16*, INT32) { return 0; }
INT32 CDEmuScan(INT32, INT32*) { return 0; }
