// cna-widget: a small Windows 11 style desktop widget + tray icon that drives cna.exe.
// It never does any scanning itself; every action runs `cna.exe ... --json` as a child process,
// so the 125 MB cap and all safety rules of the CLI apply unchanged.
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>
#include <shellapi.h>
#include <dwmapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>
using std::max;
using std::min;
#include <gdiplus.h>
using namespace Gdiplus;

// ---------------------------------------------------------------------------------------------
// constants / ids
// ---------------------------------------------------------------------------------------------
#define WM_JOB_DONE (WM_APP + 1)
#define WM_TRAY (WM_APP + 2)
#define WM_SHOWME (WM_APP + 3)
enum { T_ANIM = 1, T_CONFIRM = 2, T_DISK = 3 };
enum { ID_NONE = 0, ID_CLOSE, ID_PIN, ID_PROF = 10, ID_SCAN = 20, ID_ACTION, ID_DUPES = 30, ID_LEFT, ID_QUAR, ID_SMART, ID_DEBLOAT, ID_DETAILS = 40, ID_AUTO };
enum { CM_SHOW = 1, CM_QUICK, CM_AUTORUN, CM_PIN, CM_STARTUP, CM_UNDO_DEBLOAT, CM_REMOVE_APPS, CM_EXIT };
enum Kind { K_NONE, K_SCAN, K_CLEAN, K_QUICK, K_DUPES, K_LEFT, K_QUAR, K_QDUPES, K_QLEFT, K_AUTOSTAT, K_AUTOSET, K_AUTORUN, K_DETAILS, K_SMART, K_QSMART, K_DEBLOAT, K_DEBLOAT_APPLY, K_DEBLOAT_UNDO, K_DEBLOAT_APPS };

static const wchar_t* kClass = L"CnaWidgetWnd";
static const wchar_t* kRunKey = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static const int kW = 340, kH = 580; // logical size (dp)

// ---------------------------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------------------------
static HINSTANCE g_inst;
static HWND g_hwnd;
static HICON g_icon;
static NOTIFYICONDATAW g_nid;
static UINT g_msgTaskbar;
static float g_s = 1.0f;
static std::wstring g_dir, g_ini;
static bool g_visible = true, g_pinned = false, g_busy = false, g_confirming = false, g_autoOn = false, g_statusErr = false;
static int g_profile = 0, g_kind = K_NONE, g_hot = ID_NONE, g_down = ID_NONE;
static ULONGLONG g_t0 = 0;

struct Pending { bool on = false; std::wstring label, args; int kind = K_NONE; };
static Pending g_pending;
static std::wstring g_note;
static std::wstring g_status = L"Ready", g_sub = L"", g_heroBig = L"\u2014", g_heroCap = L"Run a scan to see what can be cleaned", g_heroMeta, g_reportArgs;
static std::vector<std::pair<std::wstring, std::wstring>> g_lines;
static uint64_t g_diskFree = 0, g_diskTotal = 0;
static bool g_diskOk = false;

static HANDLE g_child = nullptr;
static CRITICAL_SECTION g_cs;

static const wchar_t* kProfNames[5] = {L"Quick", L"Browser", L"Dev", L"Gaming", L"Full"};
static const wchar_t* kProfArgs[5] = {L"quick", L"browser", L"dev", L"gaming", L"full"};

// ---------------------------------------------------------------------------------------------
// theme
// ---------------------------------------------------------------------------------------------
struct Theme { bool dark; Color bg, card, border, text, sub, track, btn, btnH, btnP, accent, accentH, accentText, danger; };
static Theme T;
static Color C(uint32_t rgb, BYTE a = 255) { return Color(a, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
static Color mix(Color a, Color b, float t) {
  return Color(255, (BYTE)(a.GetR() + (b.GetR() - a.GetR()) * t), (BYTE)(a.GetG() + (b.GetG() - a.GetG()) * t), (BYTE)(a.GetB() + (b.GetB() - a.GetB()) * t));
}
static void load_theme() {
  DWORD v = 1, sz = sizeof v;
  RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &sz);
  T.dark = (v == 0);
  if (T.dark) {
    T.bg = C(0x202020); T.card = C(0x2B2B2B); T.border = C(0x3C3C3C); T.text = C(0xFFFFFF); T.sub = C(0xB5B5B5);
    T.track = C(0x404040); T.btn = C(0x333333); T.btnH = C(0x3D3D3D); T.btnP = C(0x2A2A2A); T.danger = C(0xFF99A4);
    T.accent = C(0x60CDFF);
  } else {
    T.bg = C(0xF3F3F3); T.card = C(0xFFFFFF); T.border = C(0xE0E0E0); T.text = C(0x1A1A1A); T.sub = C(0x616161);
    T.track = C(0xE3E3E3); T.btn = C(0xFFFFFF); T.btnH = C(0xF6F6F6); T.btnP = C(0xEEEEEE); T.danger = C(0xC42B1C);
    T.accent = C(0x0067C0);
  }
  DWORD ac = 0; sz = sizeof ac;
  if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\DWM", L"AccentColor", RRF_RT_REG_DWORD, nullptr, &ac, &sz) == ERROR_SUCCESS) {
    Color a(255, ac & 255, (ac >> 8) & 255, (ac >> 16) & 255);
    T.accent = T.dark ? mix(a, Color(255, 255, 255, 255), 0.30f) : a;
  }
  float lum = (0.299f * T.accent.GetR() + 0.587f * T.accent.GetG() + 0.114f * T.accent.GetB()) / 255.f;
  T.accentText = lum > 0.62f ? C(0x000000) : C(0xFFFFFF);
  T.accentH = T.dark ? mix(T.accent, Color(255, 255, 255, 255), 0.12f) : mix(T.accent, Color(255, 0, 0, 0), 0.12f);
}

// ---------------------------------------------------------------------------------------------
// small helpers
// ---------------------------------------------------------------------------------------------
static std::wstring W(const std::string& s) {
  if (s.empty()) return L"";
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w((size_t)n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
  return w;
}
static std::wstring human(uint64_t b) {
  const wchar_t* u[] = {L"B", L"KB", L"MB", L"GB", L"TB"};
  double v = (double)b;
  int i = 0;
  while (v >= 1024.0 && i < 4) { v /= 1024.0; ++i; }
  wchar_t buf[48];
  if (i) _snwprintf(buf, 48, L"%.1f %ls", v, u[i]); else _snwprintf(buf, 48, L"%.0f %ls", v, u[i]);
  return buf;
}
static std::wstring leaf(const std::string& path) {
  size_t p = path.find_last_of("\\/");
  return W(p == std::string::npos ? path : path.substr(p + 1));
}
static REAL SF(float v) { return v * g_s; }
static int SI(float v) { return (int)(v * g_s + 0.5f); }

// tiny JSON readers for the (known, flat) output of cna --json
static size_t kpos(const std::string& j, const char* key, size_t from = 0) {
  std::string k = std::string("\"") + key + "\":";
  size_t p = j.find(k, from);
  return p == std::string::npos ? p : p + k.size();
}
static uint64_t numat(const std::string& j, size_t p) {
  if (p == std::string::npos) return 0;
  uint64_t v = 0;
  while (p < j.size() && j[p] >= '0' && j[p] <= '9') v = v * 10 + (uint64_t)(j[p++] - '0');
  return v;
}
static std::string strat(const std::string& j, size_t p) {
  std::string o;
  if (p == std::string::npos || p >= j.size() || j[p] != '"') return o;
  for (++p; p < j.size() && j[p] != '"'; ++p) {
    if (j[p] == '\\' && p + 1 < j.size()) {
      char n = j[++p];
      if (n == 'n') o += '\n'; else if (n == 't') o += '\t'; else if (n == 'u') { o += '?'; p += 4; } else o += n;
    } else o += j[p];
  }
  return o;
}
static std::string nth_str(const std::string& j, size_t arr, size_t n) {
  if (arr == std::string::npos || arr >= j.size() || j[arr] != '[') return "";
  size_t p = arr + 1;
  for (size_t i = 0; i < n; ++i) {
    if (p >= j.size() || j[p] != '"') return "";
    for (++p; p < j.size() && j[p] != '"'; ++p) if (j[p] == '\\') ++p; // skip escaped char
    ++p;                                                                // closing quote
    if (p < j.size() && j[p] == ',') ++p;
  }
  return strat(j, p);
}
static size_t count_of(const std::string& j, const char* what) {
  size_t n = 0, p = 0, l = strlen(what);
  while ((p = j.find(what, p)) != std::string::npos) { ++n; p += l; }
  return n;
}

// ---------------------------------------------------------------------------------------------
// fonts + drawing primitives
// ---------------------------------------------------------------------------------------------
static ULONG_PTR g_gdip;
static Font *fBody, *fSmall, *fBold, *fTitle, *fHero, *fIcon, *fIconS;
static FontFamily *g_fam, *g_famIcon;
static Bitmap* g_bmp;

static void make_fonts() {
  delete fBody; delete fSmall; delete fBold; delete fTitle; delete fHero; delete fIcon; delete fIconS; delete g_fam; delete g_famIcon;
  // Prefer the Windows 11 fonts, degrade gracefully (never draw an empty widget).
  g_fam = new FontFamily(L"Segoe UI Variable Text");
  if (!g_fam->IsAvailable()) { delete g_fam; g_fam = new FontFamily(L"Segoe UI"); }
  if (!g_fam->IsAvailable()) { delete g_fam; g_fam = FontFamily::GenericSansSerif()->Clone(); }
  g_famIcon = new FontFamily(L"Segoe Fluent Icons");
  if (!g_famIcon->IsAvailable()) { delete g_famIcon; g_famIcon = new FontFamily(L"Segoe MDL2 Assets"); }
  if (!g_famIcon->IsAvailable()) { delete g_famIcon; g_famIcon = g_fam->Clone(); }
  fBody = new Font(g_fam, SF(13), FontStyleRegular, UnitPixel);
  fSmall = new Font(g_fam, SF(11.5f), FontStyleRegular, UnitPixel);
  fBold = new Font(g_fam, SF(13), FontStyleBold, UnitPixel);
  fTitle = new Font(g_fam, SF(14), FontStyleBold, UnitPixel);
  fHero = new Font(g_fam, SF(30), FontStyleBold, UnitPixel);
  fIcon = new Font(g_famIcon, SF(18), FontStyleRegular, UnitPixel);
  fIconS = new Font(g_famIcon, SF(13), FontStyleRegular, UnitPixel);
}

static void rr_path(GraphicsPath& p, RectF r, REAL rad) {
  REAL d = rad * 2;
  if (d > r.Width) d = r.Width;
  if (d > r.Height) d = r.Height;
  p.AddArc(r.X, r.Y, d, d, 180, 90);
  p.AddArc(r.GetRight() - d, r.Y, d, d, 270, 90);
  p.AddArc(r.GetRight() - d, r.GetBottom() - d, d, d, 0, 90);
  p.AddArc(r.X, r.GetBottom() - d, d, d, 90, 90);
  p.CloseFigure();
}
static void box(Graphics& g, RectF r, REAL rad, Color fill, const Color* border = nullptr) {
  GraphicsPath p;
  rr_path(p, r, rad);
  SolidBrush b(fill);
  g.FillPath(&b, &p);
  if (border) {
    RectF ri(r.X + 0.5f, r.Y + 0.5f, r.Width - 1, r.Height - 1);
    GraphicsPath p2;
    rr_path(p2, ri, max(0.f, rad - 0.5f));
    Pen pen(*border, 1.0f);
    g.DrawPath(&pen, &p2);
  }
}
static void txt(Graphics& g, const std::wstring& s, Font* f, RectF r, Color c, StringAlignment h = StringAlignmentNear, StringAlignment v = StringAlignmentCenter) {
  StringFormat sf;
  sf.SetAlignment(h);
  sf.SetLineAlignment(v);
  sf.SetTrimming(StringTrimmingEllipsisCharacter);
  sf.SetFormatFlags(StringFormatFlagsNoWrap);
  SolidBrush b(c);
  g.DrawString(s.c_str(), -1, f, r, &sf, &b);
}
static void logo(Graphics& g, RectF r) {
  box(g, r, r.Width * 0.28f, T.accent);
  PointF pts[8];
  const float u[8][2] = {{0.5f, 0.16f}, {0.58f, 0.42f}, {0.84f, 0.5f}, {0.58f, 0.58f}, {0.5f, 0.84f}, {0.42f, 0.58f}, {0.16f, 0.5f}, {0.42f, 0.42f}};
  for (int i = 0; i < 8; ++i) pts[i] = PointF(r.X + u[i][0] * r.Width, r.Y + u[i][1] * r.Height);
  SolidBrush b(T.accentText);
  g.FillPolygon(&b, pts, 8);
}
static HICON make_icon(int px) {
  Bitmap bmp(px, px, PixelFormat32bppARGB);
  Graphics g(&bmp);
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  g.Clear(Color(0, 0, 0, 0));
  Color saveA = T.accent, saveT = T.accentText;
  T.accent = C(0x0A84E8); T.accentText = C(0xFFFFFF);
  logo(g, RectF(0.5f, 0.5f, (REAL)px - 1, (REAL)px - 1));
  T.accent = saveA; T.accentText = saveT;
  HICON h = nullptr;
  bmp.GetHICON(&h);
  return h;
}

// ---------------------------------------------------------------------------------------------
// layout / buttons
// ---------------------------------------------------------------------------------------------
struct Btn { int id; RectF r; };
static std::vector<Btn> g_btns;
static RectF R(float x, float y, float w, float h) { return RectF(SF(x), SF(y), SF(w), SF(h)); }
static RectF rect_of(int id) {
  for (auto& b : g_btns) if (b.id == id) return b.r;
  return RectF();
}
static void layout() {
  g_btns.clear();
  auto add = [&](int id, float x, float y, float w, float h) { g_btns.push_back({id, R(x, y, w, h)}); };
  add(ID_CLOSE, kW - 16 - 28, 14, 28, 28);
  add(ID_PIN, kW - 16 - 28 - 32, 14, 28, 28);
  float sw = 308.f / 5.f;
  for (int i = 0; i < 5; ++i) add(ID_PROF + i, 16 + i * sw, 120, sw, 32);
  add(ID_SCAN, 16, 240, 96, 36);
  add(ID_ACTION, 120, 240, 204, 36);
  float tw = (308.f - 16.f) / 3.f;
  add(ID_DUPES, 16, 310, tw, 56);
  add(ID_LEFT, 16 + tw + 8, 310, tw, 56);
  add(ID_QUAR, 16 + 2 * (tw + 8), 310, tw, 56);
  add(ID_SMART, 16, 374, 150, 56);
  add(ID_DEBLOAT, 174, 374, 150, 56);
  add(ID_DETAILS, kW - 16 - 12 - 64, 444, 64, 20);
  add(ID_AUTO, 16, 528, 308, 36);
}
static bool enabled(int id) {
  if (id == ID_CLOSE || id == ID_PIN) return true;
  if (g_busy) return id == ID_ACTION;
  if (id == ID_ACTION) return g_pending.on;
  if (id == ID_DETAILS) return !g_reportArgs.empty();
  return true;
}
static int hit(int x, int y) {
  for (auto& b : g_btns) if (b.r.Contains((REAL)x, (REAL)y)) return b.id;
  return ID_NONE;
}
static std::wstring action_label() {
  if (g_busy) return L"Stop";
  if (g_confirming) return L"Confirm";
  if (g_pending.on) return g_pending.label;
  return L"Clean";
}

static void draw_button(Graphics& g, int id, const std::wstring& label, bool primary) {
  RectF r = rect_of(id);
  bool en = enabled(id), hot = g_hot == id && en, down = g_down == id && hot;
  Color fill, tc;
  const Color* bd = nullptr;
  Color bdc = T.border;
  if (primary && en) {
    fill = down ? mix(T.accent, Color(255, 0, 0, 0), 0.15f) : hot ? T.accentH : T.accent;
    tc = T.accentText;
  } else if (primary) {
    fill = T.track; tc = mix(T.sub, T.track, 0.35f);
  } else {
    fill = !en ? T.track : down ? T.btnP : hot ? T.btnH : T.btn;
    tc = en ? T.text : mix(T.sub, T.track, 0.35f);
    bd = &bdc;
  }
  box(g, r, SF(5), fill, bd);
  txt(g, label, fBold, r, tc, StringAlignmentCenter);
}

// ---------------------------------------------------------------------------------------------
// painting
// ---------------------------------------------------------------------------------------------
static void paint(HDC hdc) {
  RECT rc;
  GetClientRect(g_hwnd, &rc);
  int w = rc.right, h = rc.bottom;
  if (w <= 0 || h <= 0) return;
  if (!g_bmp || (int)g_bmp->GetWidth() != w || (int)g_bmp->GetHeight() != h) { delete g_bmp; g_bmp = new Bitmap(w, h, PixelFormat32bppRGB); }
  Graphics g(g_bmp);
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
  g.Clear(T.bg);

  // header
  logo(g, R(16, 14, 28, 28));
  txt(g, L"Clean-n-Adapt", fTitle, R(54, 12, 190, 20), T.text);
  txt(g, g_sub, fSmall, R(54, 31, 190, 16), T.sub);
  {
    RectF c = rect_of(ID_CLOSE), p = rect_of(ID_PIN);
    if (g_hot == ID_CLOSE) box(g, c, SF(5), g_down == ID_CLOSE ? T.btnP : T.btnH);
    if (g_hot == ID_PIN || g_pinned) box(g, p, SF(5), g_pinned ? mix(T.card, T.accent, 0.25f) : (g_down == ID_PIN ? T.btnP : T.btnH));
    txt(g, L"\uE711", fIconS, c, T.text, StringAlignmentCenter);
    txt(g, L"\uE718", fIconS, p, g_pinned ? T.accent : T.sub, StringAlignmentCenter);
  }

  // disk card
  {
    RectF r = R(16, 60, 308, 52);
    box(g, r, SF(8), T.card, &T.border);
    wchar_t drv[8] = L"C:";
    GetEnvironmentVariableW(L"SystemDrive", drv, 8);
    txt(g, std::wstring(L"Windows (") + drv + L")", fBody, R(28, 66, 150, 20), T.text);
    if (g_diskOk) {
      txt(g, human(g_diskFree) + L" free of " + human(g_diskTotal), fSmall, R(150, 66, 162, 20), T.sub, StringAlignmentFar);
      float used = g_diskTotal ? 1.0f - (float)((double)g_diskFree / (double)g_diskTotal) : 0;
      RectF tr = R(28, 92, 284, 8);
      box(g, tr, SF(4), T.track);
      RectF fr = tr;
      fr.Width = max(SF(8), tr.Width * used);
      box(g, fr, SF(4), used > 0.9f ? T.danger : T.accent);
    }
  }

  // profile chips
  {
    RectF r = R(16, 120, 308, 32);
    box(g, r, SF(6), T.track);
    for (int i = 0; i < 5; ++i) {
      RectF c = rect_of(ID_PROF + i);
      bool sel = i == g_profile;
      if (sel) { RectF in(c.X + SF(2), c.Y + SF(2), c.Width - SF(4), c.Height - SF(4)); box(g, in, SF(5), T.card, &T.border); }
      else if (g_hot == ID_PROF + i && !g_busy) { RectF in(c.X + SF(2), c.Y + SF(2), c.Width - SF(4), c.Height - SF(4)); box(g, in, SF(5), mix(T.track, T.card, 0.4f)); }
      txt(g, kProfNames[i], sel ? fBold : fBody, c, sel ? T.text : T.sub, StringAlignmentCenter);
    }
  }

  // hero
  {
    RectF r = R(16, 160, 308, 72);
    box(g, r, SF(8), T.card, &T.border);
    txt(g, g_heroBig, fHero, R(28, 164, 200, 42), T.text);
    txt(g, g_heroCap, fSmall, R(28, 206, 284, 18), T.sub);
    if (!g_heroMeta.empty()) txt(g, g_heroMeta, fSmall, R(200, 170, 112, 20), T.sub, StringAlignmentFar);
  }

  // buttons
  draw_button(g, ID_SCAN, L"Scan", false);
  draw_button(g, ID_ACTION, action_label(), true);

  // status + progress
  {
    txt(g, g_status, fSmall, R(18, 282, 304, 18), g_statusErr ? T.danger : T.sub);
    RectF tr = R(16, 302, 308, 3);
    if (g_busy) {
      box(g, tr, SF(1.5f), T.track);
      double t = (double)(GetTickCount64() - g_t0) / 1400.0;
      double ph = t - std::floor(t);
      REAL segw = tr.Width * 0.3f;
      REAL x = (REAL)(tr.X - segw + ph * (tr.Width + segw));
      REAL x0 = max(x, tr.X), x1 = min(x + segw, tr.GetRight());
      if (x1 > x0) box(g, RectF(x0, tr.Y, x1 - x0, tr.Height), SF(1.5f), T.accent);
    }
  }

  // tiles
  {
    struct Tile { int id; const wchar_t* icon; const wchar_t* label; } tiles[5] = {
        {ID_DUPES, L"\uE8C8", L"Duplicates"}, {ID_LEFT, L"\uE74D", L"Leftovers"}, {ID_QUAR, L"\uE72E", L"Quarantine"},
        {ID_SMART, L"\uE721", L"Smart advice"}, {ID_DEBLOAT, L"\uE713", L"Debloat"}};
    for (auto& t : tiles) {
      RectF r = rect_of(t.id);
      bool en = enabled(t.id), hot = g_hot == t.id && en, down = g_down == t.id && hot;
      box(g, r, SF(8), !en ? T.track : down ? T.btnP : hot ? T.btnH : T.card, &T.border);
      Color ic = en ? T.accent : T.sub;
      txt(g, t.icon, fIcon, RectF(r.X, r.Y + SF(6), r.Width, SF(26)), ic, StringAlignmentCenter);
      txt(g, t.label, fSmall, RectF(r.X, r.Y + SF(32), r.Width, SF(18)), en ? T.text : T.sub, StringAlignmentCenter);
    }
  }

  // results
  {
    RectF r = R(16, 438, 308, 84);
    box(g, r, SF(8), T.card, &T.border);
    txt(g, L"Results", fSmall, R(28, 444, 100, 20), T.sub);
    bool en = enabled(ID_DETAILS);
    RectF d = rect_of(ID_DETAILS);
    txt(g, L"Details", fSmall, d, en ? (g_hot == ID_DETAILS ? T.accentH : T.accent) : mix(T.sub, T.card, 0.5f), StringAlignmentFar);
    if (g_lines.empty()) {
      txt(g, L"Nothing to show yet.", fSmall, R(28, 468, 284, 18), T.sub);
    } else {
      float y = 466;
      for (size_t i = 0; i < g_lines.size() && i < 3; ++i, y += 17) {
        txt(g, g_lines[i].first, fSmall, R(28, y, 200, 17), T.text);
        txt(g, g_lines[i].second, fSmall, R(228, y, 84, 17), T.sub, StringAlignmentFar);
      }
    }
  }

  // auto-clean row
  {
    RectF r = rect_of(ID_AUTO);
    bool hot = g_hot == ID_AUTO && enabled(ID_AUTO);
    box(g, r, SF(8), hot ? T.btnH : T.card, &T.border);
    txt(g, L"Auto-clean in background", fBody, R(28, 527, 220, 18), T.text);
    txt(g, L"Runs every 6 hours using cna.conf", fSmall, R(28, 545, 220, 16), T.sub);
    RectF sw = R(324 - 12 - 40, 538, 40, 20);
    if (g_autoOn) {
      box(g, sw, SF(10), T.accent);
      SolidBrush thumb(T.accentText);
      g.FillEllipse(&thumb, RectF(sw.GetRight() - SF(17), sw.Y + SF(3), SF(14), SF(14)));
    } else {
      Color b2 = T.sub;
      box(g, sw, SF(10), T.card, &b2);
      SolidBrush thumb(T.sub);
      g.FillEllipse(&thumb, RectF(sw.X + SF(4), sw.Y + SF(4), SF(12), SF(12)));
    }
  }

  Graphics gw(hdc);
  gw.SetInterpolationMode(InterpolationModeNearestNeighbor);
  gw.SetPixelOffsetMode(PixelOffsetModeHalf);
  gw.DrawImage(g_bmp, 0, 0, w, h);
}

static void invalidate() { if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE); }

// ---------------------------------------------------------------------------------------------
// settings / tray / misc windows glue
// ---------------------------------------------------------------------------------------------
static void save_settings() {
  if (g_ini.empty() || !g_hwnd) return;
  RECT r;
  GetWindowRect(g_hwnd, &r);
  auto put = [&](const wchar_t* k, int v) { wchar_t b[32]; _snwprintf(b, 32, L"%d", v); WritePrivateProfileStringW(L"widget", k, b, g_ini.c_str()); };
  put(L"x", r.left); put(L"y", r.top); put(L"pinned", g_pinned); put(L"profile", g_profile); put(L"visible", g_visible);
}
static void tray_add() {
  ZeroMemory(&g_nid, sizeof g_nid);
  g_nid.cbSize = sizeof g_nid;
  g_nid.hWnd = g_hwnd;
  g_nid.uID = 1;
  g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
  g_nid.uCallbackMessage = WM_TRAY;
  g_nid.hIcon = g_icon;
  wcscpy(g_nid.szTip, L"Clean-n-Adapt");
  Shell_NotifyIconW(NIM_ADD, &g_nid);
}
static void balloon(const std::wstring& title, const std::wstring& msg) {
  NOTIFYICONDATAW n = g_nid;
  n.uFlags = NIF_INFO;
  wcsncpy(n.szInfoTitle, title.c_str(), 63);
  wcsncpy(n.szInfo, msg.c_str(), 255);
  n.dwInfoFlags = NIIF_INFO | NIIF_NOSOUND;
  Shell_NotifyIconW(NIM_MODIFY, &n);
}
static void show_widget(bool show) {
  g_visible = show;
  if (show) { ShowWindow(g_hwnd, SW_SHOW); SetForegroundWindow(g_hwnd); invalidate(); }
  else ShowWindow(g_hwnd, SW_HIDE);
  save_settings();
}
static void set_pinned(bool p) {
  g_pinned = p;
  SetWindowPos(g_hwnd, p ? HWND_TOPMOST : HWND_NOTOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
  save_settings();
  invalidate();
}
static bool startup_enabled() { return RegGetValueW(HKEY_CURRENT_USER, kRunKey, L"cna-widget", RRF_RT_REG_SZ, nullptr, nullptr, nullptr) == ERROR_SUCCESS; }
static void set_startup(bool on) {
  HKEY k;
  if (RegOpenKeyExW(HKEY_CURRENT_USER, kRunKey, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
  if (on) {
    wchar_t exe[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
    std::wstring cmd = std::wstring(L"\"") + exe + L"\"";
    RegSetValueExW(k, L"cna-widget", 0, REG_SZ, (const BYTE*)cmd.c_str(), (DWORD)((cmd.size() + 1) * sizeof(wchar_t)));
  } else RegDeleteValueW(k, L"cna-widget");
  RegCloseKey(k);
}
static void enable_dark_menus() {
  HMODULE ux = LoadLibraryW(L"uxtheme.dll");
  if (!ux) return;
  typedef int(WINAPI * SetMode)(int);
  typedef void(WINAPI * Flush)();
  auto set = reinterpret_cast<SetMode>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(135))));
  auto fl = reinterpret_cast<Flush>(reinterpret_cast<void*>(GetProcAddress(ux, MAKEINTRESOURCEA(136))));
  if (set) set(1); // AllowDark: follow the system setting
  if (fl) fl();
}
static void apply_dwm() {
  int pref = 2; // DWMWCP_ROUND
  DwmSetWindowAttribute(g_hwnd, 33, &pref, sizeof pref);
  BOOL dark = T.dark;
  DwmSetWindowAttribute(g_hwnd, 20, &dark, sizeof dark);
  COLORREF bc = RGB(T.border.GetR(), T.border.GetG(), T.border.GetB());
  DwmSetWindowAttribute(g_hwnd, 34, &bc, sizeof bc);
  MARGINS m = {1, 1, 1, 1};
  DwmExtendFrameIntoClientArea(g_hwnd, &m);
}
static void refresh_disk() {
  wchar_t drv[16] = L"C:";
  GetEnvironmentVariableW(L"SystemDrive", drv, 16);
  std::wstring root = std::wstring(drv) + L"\\";
  ULARGE_INTEGER fb, tb, tf;
  if (GetDiskFreeSpaceExW(root.c_str(), &fb, &tb, &tf)) { g_diskFree = fb.QuadPart; g_diskTotal = tb.QuadPart; g_diskOk = true; }
}

// ---------------------------------------------------------------------------------------------
// running cna.exe
// ---------------------------------------------------------------------------------------------
struct JobData { int kind; std::wstring args; std::string out; DWORD code = 0; bool started = true; bool silent = false; };

static DWORD WINAPI worker(LPVOID p) {
  JobData* j = (JobData*)p;
  SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
  HANDLE rd = nullptr, wr = nullptr;
  CreatePipe(&rd, &wr, &sa, 0);
  SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
  HANDLE nulOut = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_WRITE, &sa, OPEN_EXISTING, 0, nullptr);
  HANDLE nulIn = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ, &sa, OPEN_EXISTING, 0, nullptr);
  STARTUPINFOW si{};
  si.cb = sizeof si;
  si.dwFlags = STARTF_USESTDHANDLES;
  si.hStdOutput = wr;
  si.hStdError = nulOut;
  si.hStdInput = nulIn;
  PROCESS_INFORMATION pi{};
  std::wstring cmd = L"\"" + g_dir + L"\\cna.exe\" " + j->args;
  std::vector<wchar_t> buf(cmd.begin(), cmd.end());
  buf.push_back(0);
  BOOL ok = CreateProcessW(nullptr, buf.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
  CloseHandle(wr);
  if (nulOut != INVALID_HANDLE_VALUE) CloseHandle(nulOut);
  if (nulIn != INVALID_HANDLE_VALUE) CloseHandle(nulIn);
  if (!ok) {
    j->started = false;
    CloseHandle(rd);
  } else {
    if (!j->silent) { EnterCriticalSection(&g_cs); g_child = pi.hProcess; LeaveCriticalSection(&g_cs); }
    char tmp[8192];
    DWORD n;
    while (ReadFile(rd, tmp, sizeof tmp, &n, nullptr) && n > 0) {
      if (j->out.size() < (16u << 20)) j->out.append(tmp, n);
    }
    WaitForSingleObject(pi.hProcess, INFINITE);
    GetExitCodeProcess(pi.hProcess, &j->code);
    if (!j->silent) { EnterCriticalSection(&g_cs); g_child = nullptr; LeaveCriticalSection(&g_cs); }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
  }
  PostMessageW(g_hwnd, WM_JOB_DONE, 0, (LPARAM)j);
  return 0;
}

static void set_status(const std::wstring& s, bool err = false) { g_status = s; g_statusErr = err; }

static void start_job(int kind, const std::wstring& args, const std::wstring& busy_text) {
  bool silent = kind == K_AUTOSTAT; // background query: must never lock the UI
  if (!silent) {
    if (g_busy) return;
    g_busy = true; g_kind = kind; g_confirming = false;
    KillTimer(g_hwnd, T_CONFIRM);
    g_t0 = GetTickCount64();
    SetTimer(g_hwnd, T_ANIM, 30, nullptr);
    set_status(busy_text);
  }
  JobData* j = new JobData{kind, args, "", 0, true, silent};
  HANDLE h = CreateThread(nullptr, 0, worker, j, 0, nullptr);
  if (h) CloseHandle(h);
  invalidate();
}

static std::wstring prof_arg() { return kProfArgs[g_profile]; }

static void clear_result() {
  g_pending = Pending();
  g_lines.clear();
  g_heroBig = L"\u2014"; g_heroCap = L"Run a scan to see what can be cleaned"; g_heroMeta.clear();
  g_reportArgs.clear();
  set_status(L"Ready");
}

static void on_done(JobData* j) {
  bool silent = j->silent;
  if (!silent) { g_busy = false; g_kind = K_NONE; g_confirming = false; KillTimer(g_hwnd, T_ANIM); }
  const std::string& o = j->out;
  bool json = !o.empty() && (o.find('{') != std::string::npos || j->kind == K_DEBLOAT_UNDO);
  int kind = j->kind;
  if (!silent) g_statusErr = false;

  auto fail = [&](const wchar_t* what) {
    if (!j->started) set_status(L"cna.exe not found next to the widget", true);
    else if (j->code == 3) set_status(L"Stopped: memory cap reached. Try a narrower scan.", true);
    else if (j->code == 0xC000013A || j->code == 1 || j->code == 0xFFFFFFFF) set_status(std::wstring(what) + L" stopped or failed", true);
    else { wchar_t b[96]; _snwprintf(b, 96, L"%ls failed (exit code %lu)", what, (unsigned long)j->code); set_status(b, true); }
  };

  if (kind == K_AUTOSTAT) {
    g_autoOn = o.find("Schedule: installed") != std::string::npos;
    size_t p = o.find("Last run: ");
    std::wstring last;
    if (p != std::string::npos) { size_t e = o.find('\n', p); last = W(o.substr(p + 10, e == std::string::npos ? e : e - p - 10)); while (!last.empty() && (last.back() == L'\r' || last.back() == L' ')) last.pop_back(); }
    g_sub = (g_autoOn ? L"Auto-clean on" : L"Auto-clean off");
    if (!last.empty() && last != L"never") g_sub += L" \u00B7 last run " + last.substr(0, 16);
    if (!g_note.empty()) { set_status(g_note, g_note.find(L"Could not") == 0); g_note.clear(); }
    else if (!g_busy && !g_pending.on && g_lines.empty()) set_status(L"Ready");
  } else if (kind == K_AUTOSET) {
    g_note = j->code == 0 ? (g_autoOn ? L"Auto-clean disabled" : L"Auto-clean enabled") : L"Could not change the scheduled task";
    start_job(K_AUTOSTAT, L"auto status", g_note);
    delete j;
    invalidate();
    return;
  } else if (kind == K_AUTORUN) {
    std::wstring first = W(o.substr(0, o.find('\n')));
    set_status(first.empty() ? L"Auto-clean finished" : first);
    if (!g_visible) balloon(L"Clean-n-Adapt", first.empty() ? L"Auto-clean finished" : first);
    refresh_disk();
  } else if (kind == K_DETAILS) {
    if (!j->started || o.empty()) fail(L"Report");
    else {
      wchar_t tmp[MAX_PATH];
      GetTempPathW(MAX_PATH, tmp);
      std::wstring path = std::wstring(tmp) + L"cna-report.txt";
      HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
      if (f != INVALID_HANDLE_VALUE) { DWORD w; WriteFile(f, o.data(), (DWORD)o.size(), &w, nullptr); CloseHandle(f); ShellExecuteW(nullptr, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
      set_status(L"Report opened");
    }
  } else if (!json) {
    fail(kind == K_SCAN ? L"Scan" : kind == K_DUPES ? L"Duplicate search" : kind == K_LEFT ? L"Leftover search" : L"Action");
  } else if (kind == K_SCAN) {
    uint64_t bytes = numat(o, kpos(o, "total_bytes")), files = numat(o, kpos(o, "total_files"));
    struct Row { std::string name; uint64_t b; };
    std::vector<Row> rows;
    size_t p = 0;
    while ((p = o.find("{\"rule\":\"", p)) != std::string::npos) {
      Row r{strat(o, p + 8), numat(o, kpos(o, "bytes", p))};
      if (r.b) rows.push_back(r);
      p += 9;
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) { return a.b > b.b; });
    g_lines.clear();
    for (auto& r : rows) g_lines.push_back({W(r.name), human(r.b)});
    g_heroBig = human(bytes);
    g_heroCap = std::wstring(L"can be reclaimed \u00B7 ") + kProfNames[g_profile] + L" profile";
    g_heroMeta = std::to_wstring(files) + L" files";
    g_reportArgs = L"scan --profile " + prof_arg() + L" -v";
    size_t sk = kpos(o, "skipped_needs_admin");
    size_t nadmin = 0;
    if (sk != std::string::npos) { size_t e = o.find(']', sk); if (e != std::string::npos && e > sk + 1) nadmin = count_of(o.substr(sk, e - sk), "\",\"") + 1; }
    if (bytes > 0) {
      g_pending = {true, L"Clean " + human(bytes), L"clean --profile " + prof_arg() + L" --yes --json", K_CLEAN};
      set_status(nadmin ? std::to_wstring(nadmin) + L" rule(s) need administrator and were skipped" : L"Review, then clean");
    } else { g_pending = Pending(); set_status(L"Nothing to clean here"); }
  } else if (kind == K_CLEAN || kind == K_QUICK) {
    uint64_t bytes = numat(o, kpos(o, "total_bytes")), failed = numat(o, kpos(o, "failed"));
    g_pending = Pending(); g_lines.clear(); g_reportArgs.clear();
    g_heroBig = human(bytes);
    g_heroCap = failed ? std::to_wstring(failed) + L" files were in use and skipped" : L"freed just now";
    g_heroMeta.clear();
    set_status(L"Freed " + human(bytes));
    if (!g_visible || kind == K_QUICK) balloon(L"Clean-n-Adapt", L"Freed " + human(bytes));
    refresh_disk();
  } else if (kind == K_DUPES) {
    uint64_t wasted = numat(o, kpos(o, "total_wasted"));
    size_t nsets = count_of(o, "\"keeper\":");
    g_lines.clear();
    size_t p = 0;
    int shown = 0;
    while (shown < 3 && (p = o.find("{\"size\":", p)) != std::string::npos) {
      uint64_t sz = numat(o, kpos(o, "size", p)), wasted_i = numat(o, kpos(o, "wasted", p));
      size_t pp = kpos(o, "paths", p);
      std::string first = nth_str(o, pp, (size_t)numat(o, kpos(o, "keeper", p))); // show the copy that is kept
      size_t e = o.find(']', pp);
      size_t copies = (e != std::string::npos) ? count_of(o.substr(pp, e - pp), "\",\"") + 1 : 1;
      (void)sz;
      g_lines.push_back({leaf(first) + L"  \u00D7" + std::to_wstring(copies), human(wasted_i)});
      p += 8; ++shown;
    }
    g_heroBig = human(wasted);
    g_heroCap = std::to_wstring(nsets) + (nsets == 1 ? L" set of duplicate files (10 MB+)" : L" sets of duplicate files (10 MB+)");
    g_heroMeta.clear();
    g_reportArgs = L"dupes --min-size 10M --limit 100";
    if (nsets) { g_pending = {true, L"Quarantine extras", L"dupes --min-size 10M --quarantine --yes --json", K_QDUPES}; set_status(L"Extra copies move to quarantine (reversible)"); }
    else { g_pending = Pending(); set_status(L"No duplicates found"); }
  } else if (kind == K_LEFT) {
    size_t n = count_of(o, "\"score\":");
    uint64_t total = numat(o, kpos(o, "total_bytes"));
    g_lines.clear();
    size_t p = 0;
    int shown = 0;
    while (shown < 3 && (p = o.find("{\"path\":\"", p)) != std::string::npos) {
      std::string path = strat(o, p + 8);
      uint64_t sc = numat(o, kpos(o, "score", p)), b = numat(o, kpos(o, "bytes", p));
      g_lines.push_back({leaf(path) + L"  [" + std::to_wstring(sc) + L"]", human(b)});
      p += 9; ++shown;
    }
    g_heroBig = human(total);
    g_heroCap = std::to_wstring(n) + (n == 1 ? L" folder left behind by removed apps" : L" folders left behind by removed apps");
    g_heroMeta.clear();
    g_reportArgs = L"remnants";
    if (n) { g_pending = {true, L"Quarantine leftovers", L"remnants --quarantine --yes --json", K_QLEFT}; set_status(L"Reversible: items move to quarantine"); }
    else { g_pending = Pending(); set_status(L"No leftovers found"); }
  } else if (kind == K_QUAR) {
    uint64_t total = numat(o, kpos(o, "total_bytes"));
    size_t n = count_of(o, "\"id\":\"");
    g_lines.clear();
    size_t p = 0;
    int shown = 0;
    while (shown < 3 && (p = o.find("{\"id\":\"", p)) != std::string::npos) {
      g_lines.push_back({leaf(strat(o, kpos(o, "original", p))), human(numat(o, kpos(o, "bytes", p)))});
      p += 7; ++shown;
    }
    g_pending = Pending();
    g_heroBig = human(total);
    g_heroCap = std::to_wstring(n) + L" item(s) held in quarantine";
    g_heroMeta.clear();
    g_reportArgs = L"quarantine list";
    set_status(n ? L"Restore with: cna quarantine restore <id>" : L"Quarantine is empty");
  } else if (kind == K_QDUPES || kind == K_QLEFT) {
    uint64_t b = kind == K_QDUPES ? numat(o, kpos(o, "total_wasted")) : numat(o, kpos(o, "quarantined_bytes"));
    g_pending = Pending(); g_lines.clear(); g_reportArgs.clear();
    g_heroBig = human(b);
    g_heroCap = L"moved to quarantine - undo any time";
    g_heroMeta.clear();
    set_status(L"Moved " + human(b) + L" to quarantine");
  } else if (kind == K_SMART) {
    uint64_t clean_b = numat(o, kpos(o, "clean_bytes")), review_b = numat(o, kpos(o, "review_bytes"));
    struct Row { std::string path, action; uint64_t b; };
    std::vector<Row> clean_rows, review_rows;
    size_t p = 0;
    while ((p = o.find("{\"path\":\"", p)) != std::string::npos) {
      Row r{strat(o, p + 8), strat(o, kpos(o, "action", p)), numat(o, kpos(o, "bytes", p))};
      (r.action == "clean" ? clean_rows : review_rows).push_back(r);
      p += 9;
    }
    g_lines.clear();
    for (auto& r : (clean_rows.empty() ? review_rows : clean_rows)) { g_lines.push_back({leaf(r.path), human(r.b)}); if (g_lines.size() >= 3) break; }
    g_heroBig = human(clean_b);
    g_heroCap = std::to_wstring(clean_rows.size()) + (clean_rows.size() == 1 ? L" folder suggested \u00B7 " : L" folders suggested \u00B7 ") + human(review_b) + L" to review";
    g_heroMeta.clear();
    g_reportArgs = L"recommend --limit 100";
    if (clean_b > 0) { g_pending = {true, L"Quarantine " + human(clean_b), L"recommend --apply --yes --json", K_QSMART}; set_status(L"Suggestions move to quarantine (restorable)"); }
    else { g_pending = Pending(); set_status(L"Nothing safe to suggest right now"); }
  } else if (kind == K_QSMART) {
    uint64_t b = numat(o, kpos(o, "quarantined_bytes")), n = numat(o, kpos(o, "quarantined"));
    g_pending = Pending(); g_lines.clear(); g_reportArgs.clear();
    g_heroBig = human(b); g_heroCap = std::to_wstring(n) + L" folder(s) moved to quarantine - restoring teaches the model"; g_heroMeta.clear();
    set_status(L"Moved " + human(b) + L" to quarantine");
  } else if (kind == K_DEBLOAT) {
    uint64_t safe = numat(o, kpos(o, "pending_safe")), bal = numat(o, kpos(o, "pending_balanced")), agg = numat(o, kpos(o, "pending_aggressive"));
    uint64_t done = numat(o, kpos(o, "applied", o.find("\"summary\""))), adm = numat(o, kpos(o, "need_admin"));
    g_lines.clear();
    size_t p = 0, napps = 0;
    while ((p = o.find("{\"id\":\"", p)) != std::string::npos) {
      std::string cat = strat(o, kpos(o, "category", p)), st = strat(o, kpos(o, "status", p)), title = strat(o, kpos(o, "title", p));
      uint64_t tier = numat(o, kpos(o, "tier", p));
      if (st == "pending" && cat == "apps") ++napps;
      if (st == "pending" && tier == 1 && cat != "apps" && g_lines.size() < 3) g_lines.push_back({W(title), W(cat)});
      p += 7;
    }
    g_heroBig = std::to_wstring(safe) + L" safe";
    g_heroCap = L"balanced " + std::to_wstring(bal) + L" \u00B7 aggressive " + std::to_wstring(agg) + L" \u00B7 " + std::to_wstring(done) + L" applied";
    g_heroMeta = napps ? std::to_wstring(napps) + L" Store apps" : L"";
    g_reportArgs = L"debloat scan -v";
    if (safe > 0) {
      g_pending = {true, L"Apply " + std::to_wstring(safe) + L" safe tweaks", L"debloat apply --preset safe --yes --json", K_DEBLOAT_APPLY};
      set_status(adm ? std::to_wstring(adm) + L" item(s) need administrator (skipped)" : L"Reversible - undo from the tray menu");
    } else { g_pending = Pending(); set_status(napps ? L"Store apps can be removed from the tray menu" : L"Nothing safe left to apply"); }
  } else if (kind == K_DEBLOAT_APPLY || kind == K_DEBLOAT_APPS) {
    uint64_t n = numat(o, kpos(o, "applied", o.find("\"dry_run\""))), bad = numat(o, kpos(o, "failed"));
    g_pending = Pending(); g_lines.clear(); g_reportArgs.clear();
    g_heroBig = std::to_wstring(n); g_heroCap = kind == K_DEBLOAT_APPS ? L"Store app(s) removed" : L"changes applied \u00B7 all undoable"; g_heroMeta.clear();
    set_status(bad ? std::to_wstring(bad) + L" item(s) failed (see Details)" : (kind == K_DEBLOAT_APPS ? L"Apps removed - reinstall from the Microsoft Store" : L"Applied - undo from the tray menu"), bad != 0);
    if (!g_visible) balloon(L"Clean-n-Adapt", kind == K_DEBLOAT_APPS ? L"Removed " + std::to_wstring(n) + L" app(s)" : L"Applied " + std::to_wstring(n) + L" debloat change(s)");
  } else if (kind == K_DEBLOAT_UNDO) {
    size_t q = o.find(" restored,");
    uint64_t n = 0;
    if (q != std::string::npos) { size_t b = q; while (b > 0 && o[b - 1] >= '0' && o[b - 1] <= '9') --b; n = numat(o, b); }
    g_pending = Pending(); g_lines.clear(); g_reportArgs.clear();
    g_heroBig = std::to_wstring(n); g_heroCap = L"debloat changes restored"; g_heroMeta.clear();
    set_status(L"Restored the previous settings");
  }
  delete j;
  invalidate();
}

// ---------------------------------------------------------------------------------------------
// actions
// ---------------------------------------------------------------------------------------------
static void do_click(int id) {
  if (id >= ID_PROF && id < ID_PROF + 5) {
    if (!g_busy && g_profile != id - ID_PROF) { g_profile = id - ID_PROF; clear_result(); save_settings(); }
    return;
  }
  switch (id) {
    case ID_CLOSE: show_widget(false); break;
    case ID_PIN: set_pinned(!g_pinned); break;
    case ID_SCAN: if (!g_busy) start_job(K_SCAN, L"scan --profile " + prof_arg() + L" --json", L"Scanning\u2026"); break;
    case ID_DUPES: if (!g_busy) start_job(K_DUPES, L"dupes --min-size 10M --json", L"Looking for duplicates\u2026"); break;
    case ID_LEFT: if (!g_busy) start_job(K_LEFT, L"remnants --json", L"Looking for leftovers\u2026"); break;
    case ID_QUAR: if (!g_busy) start_job(K_QUAR, L"quarantine list --json", L"Reading quarantine\u2026"); break;
    case ID_SMART: if (!g_busy) start_job(K_SMART, L"recommend --json", L"Analysing folders\u2026"); break;
    case ID_DEBLOAT: if (!g_busy) start_job(K_DEBLOAT, L"debloat scan --json", L"Checking Windows\u2026"); break;
    case ID_DETAILS: if (!g_busy && !g_reportArgs.empty()) start_job(K_DETAILS, g_reportArgs, L"Preparing report\u2026"); break;
    case ID_AUTO:
      if (!g_busy) start_job(K_AUTOSET, g_autoOn ? L"auto remove" : L"auto install --every-hours 6", g_autoOn ? L"Removing scheduled task\u2026" : L"Creating scheduled task\u2026");
      break;
    case ID_ACTION:
      if (g_busy) {
        EnterCriticalSection(&g_cs);
        if (g_child) TerminateProcess(g_child, 1);
        LeaveCriticalSection(&g_cs);
        set_status(L"Stopping\u2026");
      } else if (g_pending.on) {
        if (!g_confirming) { g_confirming = true; SetTimer(g_hwnd, T_CONFIRM, 5000, nullptr); set_status(L"Click Confirm to proceed (" + g_pending.label + L")"); }
        else { Pending p = g_pending; start_job(p.kind, p.args, p.kind == K_CLEAN ? L"Cleaning\u2026" : p.kind == K_DEBLOAT_APPLY ? L"Applying\u2026" : L"Moving to quarantine\u2026"); }
      }
      break;
  }
  invalidate();
}

static void tray_menu() {
  HMENU m = CreatePopupMenu();
  AppendMenuW(m, MF_STRING, CM_SHOW, g_visible ? L"Hide widget" : L"Show widget");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING | (g_busy ? MF_GRAYED : 0), CM_QUICK, L"Quick clean now");
  AppendMenuW(m, MF_STRING | (g_busy ? MF_GRAYED : 0), CM_AUTORUN, L"Run auto-clean now");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING | (g_pinned ? MF_CHECKED : 0), CM_PIN, L"Keep widget on top");
  AppendMenuW(m, MF_STRING | (startup_enabled() ? MF_CHECKED : 0), CM_STARTUP, L"Start with Windows");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING | (g_busy ? MF_GRAYED : 0), CM_REMOVE_APPS, L"Remove bloat apps\u2026");
  AppendMenuW(m, MF_STRING | (g_busy ? MF_GRAYED : 0), CM_UNDO_DEBLOAT, L"Undo debloat changes\u2026");
  AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
  AppendMenuW(m, MF_STRING, CM_EXIT, L"Exit");
  POINT pt;
  GetCursorPos(&pt);
  SetForegroundWindow(g_hwnd);
  TrackPopupMenu(m, TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, g_hwnd, nullptr);
  PostMessageW(g_hwnd, WM_NULL, 0, 0);
  DestroyMenu(m);
}

static void place_window(int x, int y, bool have) {
  RECT wa;
  SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
  int w = SI((float)kW), h = SI((float)kH);
  if (!have || !MonitorFromPoint(POINT{x + 20, y + 20}, MONITOR_DEFAULTTONULL)) { x = wa.right - w - SI(16); y = wa.bottom - h - SI(16); }
  SetWindowPos(g_hwnd, g_pinned ? HWND_TOPMOST : HWND_NOTOPMOST, x, y, w, h, SWP_NOACTIVATE);
}

static void update_scale() {
  typedef UINT(WINAPI * GetDpi)(HWND);
  auto gd = reinterpret_cast<GetDpi>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "GetDpiForWindow")));
  UINT dpi = gd ? gd(g_hwnd) : 96;
  g_s = dpi / 96.0f;
  make_fonts();
  layout();
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  if (m == g_msgTaskbar && g_msgTaskbar) { tray_add(); return 0; }
  switch (m) {
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
      PAINTSTRUCT ps;
      HDC dc = BeginPaint(h, &ps);
      paint(dc);
      EndPaint(h, &ps);
      return 0;
    }
    case WM_NCHITTEST: {
      POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
      ScreenToClient(h, &p);
      if (p.y >= 0 && p.y < SI(56) && hit(p.x, p.y) == ID_NONE && p.x >= 0 && p.x < SI((float)kW)) return HTCAPTION;
      return HTCLIENT;
    }
    case WM_MOUSEMOVE: {
      int id = hit(GET_X_LPARAM(l), GET_Y_LPARAM(l));
      if (id != g_hot) {
        g_hot = id;
        TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
        TrackMouseEvent(&t);
        invalidate();
      }
      return 0;
    }
    case WM_MOUSELEAVE: g_hot = ID_NONE; g_down = ID_NONE; invalidate(); return 0;
    case WM_LBUTTONDOWN: {
      int id = hit(GET_X_LPARAM(l), GET_Y_LPARAM(l));
      if (id != ID_NONE && enabled(id)) { g_down = id; SetCapture(h); invalidate(); }
      return 0;
    }
    case WM_LBUTTONUP: {
      int id = hit(GET_X_LPARAM(l), GET_Y_LPARAM(l));
      int was = g_down;
      g_down = ID_NONE;
      if (GetCapture() == h) ReleaseCapture();
      if (was != ID_NONE && was == id && enabled(id)) do_click(id);
      invalidate();
      return 0;
    }
    case WM_KEYDOWN: if (w == VK_ESCAPE) show_widget(false); return 0;
    case WM_TIMER:
      if (w == T_ANIM) { RECT r{0, SI(280), SI((float)kW), SI(308)}; InvalidateRect(h, &r, FALSE); }
      else if (w == T_CONFIRM) { KillTimer(h, T_CONFIRM); g_confirming = false; set_status(L"Ready"); invalidate(); }
      else if (w == T_DISK) { refresh_disk(); invalidate(); }
      return 0;
    case WM_JOB_DONE: on_done((JobData*)l); return 0;
    case WM_TRAY:
      if (LOWORD(l) == WM_LBUTTONUP) show_widget(!g_visible);
      else if (LOWORD(l) == WM_RBUTTONUP) tray_menu();
      return 0;
    case WM_SHOWME: show_widget(true); return 0;
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case CM_SHOW: show_widget(!g_visible); break;
        case CM_QUICK: start_job(K_QUICK, L"clean --profile quick --yes --json", L"Quick clean\u2026"); break;
        case CM_AUTORUN: start_job(K_AUTORUN, L"auto run --force", L"Running auto-clean\u2026"); break;
        case CM_PIN: set_pinned(!g_pinned); break;
        case CM_STARTUP: set_startup(!startup_enabled()); break;
        case CM_REMOVE_APPS:
          if (MessageBoxW(h, L"Remove preinstalled Store apps such as News, Solitaire, Tips, Feedback Hub and promoted games?\n\nThis is the one change Clean-n-Adapt cannot undo. You can reinstall any app from the Microsoft Store.", L"Remove bloat apps", MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) == IDYES)
            start_job(K_DEBLOAT_APPS, L"debloat apply --preset safe --only apps --yes --json", L"Removing apps\u2026");
          break;
        case CM_UNDO_DEBLOAT:
          if (MessageBoxW(h, L"Restore the previous values for every debloat change (privacy tweaks, startup items, services)?\n\nRemoved Store apps are not affected.", L"Undo debloat changes", MB_YESNO | MB_ICONQUESTION) == IDYES)
            start_job(K_DEBLOAT_UNDO, L"debloat undo --all --yes", L"Restoring settings\u2026");
          break;
        case CM_EXIT: DestroyWindow(h); break;
      }
      return 0;
    case WM_DPICHANGED: {
      RECT* r = (RECT*)l;
      update_scale();
      SetWindowPos(h, nullptr, r->left, r->top, SI((float)kW), SI((float)kH), SWP_NOZORDER | SWP_NOACTIVATE);
      invalidate();
      return 0;
    }
    case WM_SETTINGCHANGE:
      if (l && lstrcmpW((LPCWSTR)l, L"ImmersiveColorSet") == 0) { load_theme(); apply_dwm(); invalidate(); }
      return 0;
    case WM_EXITSIZEMOVE: save_settings(); return 0;
    case WM_CLOSE: show_widget(false); return 0;
    case WM_DESTROY:
      Shell_NotifyIconW(NIM_DELETE, &g_nid);
      PostQuitMessage(0);
      return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmd, int) {
  g_inst = hi;
  InitializeCriticalSection(&g_cs);
  bool quit = cmd && wcsstr(cmd, L"--quit") != nullptr; // used by the installer/uninstaller
  HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Local\\cna-widget-singleton");
  if (GetLastError() == ERROR_ALREADY_EXISTS) {
    HWND other = FindWindowW(kClass, nullptr);
    if (other) PostMessageW(other, quit ? (UINT)WM_COMMAND : (UINT)WM_SHOWME, quit ? (WPARAM)CM_EXIT : 0, 0);
    return 0;
  }
  if (quit) return 0; // nothing was running
  typedef BOOL(WINAPI * SetDpiCtx)(HANDLE);
  auto sdc = reinterpret_cast<SetDpiCtx>(reinterpret_cast<void*>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")));
  if (sdc) sdc((HANDLE)-4); // per-monitor v2

  wchar_t exe[MAX_PATH * 2];
  GetModuleFileNameW(nullptr, exe, MAX_PATH * 2);
  g_dir = exe;
  g_dir = g_dir.substr(0, g_dir.find_last_of(L"\\/"));
  {
    // Same rule as cna.exe: an existing ".state" folder next to the exe means portable mode,
    // otherwise settings live in %LOCALAPPDATA%\cna (survives upgrades/uninstalls of the program folder).
    std::wstring d = g_dir + L"\\.state";
    DWORD a = GetFileAttributesW(d.c_str());
    if (a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY)) g_ini = d + L"\\widget.ini";
    else {
      wchar_t la[MAX_PATH];
      if (GetEnvironmentVariableW(L"LOCALAPPDATA", la, MAX_PATH)) { std::wstring d2 = std::wstring(la) + L"\\cna"; CreateDirectoryW(d2.c_str(), nullptr); g_ini = d2 + L"\\widget.ini"; }
    }
  }

  GdiplusStartupInput gsi;
  GdiplusStartup(&g_gdip, &gsi, nullptr);
  load_theme();
  enable_dark_menus();

  g_pinned = GetPrivateProfileIntW(L"widget", L"pinned", 0, g_ini.c_str()) != 0;
  g_profile = max(0, min(4, (int)GetPrivateProfileIntW(L"widget", L"profile", 0, g_ini.c_str())));
  bool wantVisible = GetPrivateProfileIntW(L"widget", L"visible", 1, g_ini.c_str()) != 0;
  int sx = GetPrivateProfileIntW(L"widget", L"x", -32000, g_ini.c_str()), sy = GetPrivateProfileIntW(L"widget", L"y", -32000, g_ini.c_str());

  WNDCLASSEXW wc;
  ZeroMemory(&wc, sizeof wc);
  wc.cbSize = sizeof wc;
  wc.lpfnWndProc = WndProc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  wc.lpszClassName = kClass;
  RegisterClassExW(&wc);
  g_hwnd = CreateWindowExW(WS_EX_TOOLWINDOW | (g_pinned ? WS_EX_TOPMOST : 0), kClass, L"Clean-n-Adapt", WS_POPUP, 0, 0, 100, 100, nullptr, nullptr, hi, nullptr);
  if (!g_hwnd) return 1;
  update_scale();
  apply_dwm();
  g_icon = make_icon(GetSystemMetrics(SM_CXSMICON));
  SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_icon);
  g_msgTaskbar = RegisterWindowMessageW(L"TaskbarCreated");
  tray_add();
  refresh_disk();
  place_window(sx, sy, sx != -32000);
  SetTimer(g_hwnd, T_DISK, 30000, nullptr);
  g_visible = wantVisible;
  if (g_visible) ShowWindow(g_hwnd, SW_SHOWNOACTIVATE);
  start_job(K_AUTOSTAT, L"auto status", L"Ready");

  MSG msg;
  while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  GdiplusShutdown(g_gdip);
  if (mtx) CloseHandle(mtx);
  return 0;
}
