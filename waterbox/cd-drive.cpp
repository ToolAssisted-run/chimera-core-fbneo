// The Neo Geo CD's drive. The image itself is read by FBNeo's own reader
// (extern/FBNeo/src/intf/cd/cd_img.cpp: a cue sheet with its track files, or
// a .chd), compiled as it is; this is the part a frontend puts around it -
// which image is in the drive, the calls the Neo Geo driver makes, and the
// text helpers the reader borrows from the frontend's code.
// SPDX-License-Identifier: MIT
#include "burner.h"
#include "cd_interface.h"

extern struct CDEmuDo cdimgDo;

TCHAR CDEmuImage[MAX_PATH] = "";
UINT8 CDEmuImageTOCSHA1[MAX_PATH] = {};
CDEmuStatusValue CDEmuStatus = idle;

static bool s_open;

INT32 CDEmuInit()
{
  CDEmuStatus = idle;
  if (cdimgDo.CDEmuInit() != 0)
    return 1;
  s_open = true;
  return 0;
}

INT32 CDEmuExit()
{
  if (!s_open)
    return 1;
  s_open = false;
  return cdimgDo.CDEmuExit();
}

// With no disc every call answers as FBNeo's own interface does when its
// module is not started: the arcade machines never have one.
INT32 CDEmuStop() { return s_open ? cdimgDo.CDEmuStop() : 1; }
INT32 CDEmuPlay(UINT8 m, UINT8 s, UINT8 f) { return s_open ? cdimgDo.CDEmuPlay(m, s, f) : 1; }
INT32 CDEmuLoadSector(INT32 lba, char* buffer) { return s_open ? cdimgDo.CDEmuLoadSector(lba, buffer) : 0; }
INT32 CDEmuReadDataSector(INT32 lba, UINT8* buffer) { return s_open ? cdimgDo.CDEmuReadDataSector(lba, buffer) : 1; }
UINT8* CDEmuReadTOC(INT32 track) { return s_open ? cdimgDo.CDEmuReadTOC(track) : nullptr; }
UINT8* CDEmuReadQChannel() { return s_open ? cdimgDo.CDEmuReadQChannel() : nullptr; }
INT32 CDEmuGetSoundBuffer(INT16* buffer, INT32 samples) { return s_open ? cdimgDo.CDEmuGetSoundBuffer(buffer, samples) : 1; }
INT32 CDEmuSetVolume(double volume) { return s_open ? cdimgDo.CDEmuSetVolume(volume) : 1; }
INT32 CDEmuGetCurrentLBA() { return s_open ? cdimgDo.CDEmuGetCurrentLBA() : 0; }
INT32 CDEmuScan(INT32 action, INT32* min) { return s_open ? cdimgDo.CDEmuScan(action, min) : 1; }

// The frontend's list of known discs (titles for its window): none
void NeoCDInfo_Exit() {}

// ---- the frontend's text helpers, as the reader uses them -------------------

// The last path component, from the separator before it - or the whole name
// when it has none (the frontend's own steps one character before the string
// then, which its callers never notice because a frontend's paths are full
// ones; a disc mounted in the sandbox is a bare name).
TCHAR* ExtractFilename(TCHAR* fullname)
{
  TCHAR* filename = fullname + strlen(fullname);
  while (filename > fullname && *filename != '\\' && *filename != '/' && *filename != ':')
    filename--;
  return filename;
}

// does the name end in this extension (".cue"), whatever its case
bool IsFileExt(TCHAR* str, TCHAR* ext)
{
  const TCHAR* dot = strrchr(str, '.');
  return strcasecmp(ext, dot ? dot : str) == 0;
}

// the text after a label a line starts with; null when it starts otherwise
TCHAR* LabelCheck(TCHAR* s, TCHAR* label)
{
  if (!s || !label)
    return nullptr;
  while (isspace((unsigned char)*s))
    s++;
  const size_t n = strlen(label);
  return strncmp(s, label, n) ? nullptr : s + n;
}

// a word, or a quoted string with its quotes taken off (a track file's name)
INT32 QuoteRead(TCHAR** quote, TCHAR** end, TCHAR* src)
{
  static TCHAR text[MAX_PATH];
  TCHAR* s = src;
  while (isspace((unsigned char)*s))
    s++;
  TCHAR* e = s;
  if (*s == '"')
  {
    s++;
    e++;
    while (*e && *e != '"')
      e++;
  }
  else
  {
    while (*e && !isspace((unsigned char)*e))
      e++;
  }
  size_t n = size_t(e - s);
  if (n >= sizeof text)
    n = sizeof text - 1;
  memcpy(text, s, n);
  text[n] = '\0';
  if (*e == '"')
    e++;
  if (quote)
    *quote = text;
  if (end)
    *end = e;
  return 0;
}
