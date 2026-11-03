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
#include <commctrl.h>

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
enum { ID_NONE = 0, ID_CLOSE, ID_PIN, ID_PROF = 10, ID_SCAN = 20, ID_ACTION, ID_DUPES = 30, ID_LEFT, ID_QUAR, ID_SMART, ID_DEBLOAT, ID_APPS, ID_DETAILS = 40, ID_AUTO, ID_RESULT_ROW = 60 };
enum { CM_SHOW = 1, CM_QUICK, CM_AUTORUN, CM_PIN, CM_STARTUP, CM_UNDO_DEBLOAT, CM_REMOVE_APPS, CM_EXIT };
enum Kind { K_NONE, K_SCAN, K_CLEAN, K_QUICK, K_DUPES, K_LEFT, K_QUAR, K_QDUPES, K_QLEFT, K_AUTOSTAT, K_AUTOSET, K_AUTORUN, K_DETAILS, K_SMART, K_QSMART, K_DEBLOAT, K_DEBLOAT_APPLY, K_DEBLOAT_UNDO, K_DEBLOAT_APPS, K_APPS, K_APP_REMOVE, K_APP_LEFT, K_QAPPS, K_REVIEW };

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
struct ProfileResult {
  bool valid = false;
  Pending pending;
  std::wstring status, heroBig, heroCap, heroMeta, reportArgs;
  std::vector<std::pair<std::wstring, std::wstring>> lines;
};
static ProfileResult g_profileResults[5];
struct ReviewItem {
  std::wstring path, category, confidence, reason, command;
  uint64_t bytes = 0;
  bool eligible = false, selected = false;
};
static std::vector<ReviewItem> g_reviewItems;
static int g_reviewApplyKind = K_NONE;
static std::wstring g_reviewBaseArgs;
static HWND g_reviewWnd = nullptr, g_reviewList = nullptr;
static HWND g_reportWnd = nullptr, g_reportEdit = nullptr;
static std::wstring g_reportText;
struct Prompt { std::wstring title, body, action, args, busy; int kind = K_NONE; };
static HWND g_promptWnd = nullptr;
static Prompt g_prompt;
static std::wstring g_note;
static std::wstring g_status = L"Ready", g_sub = L"", g_heroBig = L"\u2014", g_heroCap = L"Run a scan to see what can be cleaned", g_heroMeta, g_reportArgs;
static std::vector<std::pair<std::wstring, std::wstring>> g_lines;
static bool g_selectable = false, g_profileResultShown = false;
static std::vector<std::wstring> g_selectPaths;
static std::vector<bool> g_selected;
static std::wstring g_selectBaseArgs;
static int g_selectKind = K_NONE;
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
static HFONT g_uiFont = nullptr, g_uiSmallFont = nullptr, g_uiBoldFont = nullptr;
static HBRUSH g_popupBrush = nullptr, g_popupCardBrush = nullptr;
static Color C(uint32_t rgb, BYTE a = 255) { return Color(a, (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255); }
static COLORREF CR(Color c) { return RGB(c.GetR(), c.GetG(), c.GetB()); }
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
  if (g_popupBrush) DeleteObject(g_popupBrush);
  if (g_popupCardBrush) DeleteObject(g_popupCardBrush);
  g_popupBrush = CreateSolidBrush(CR(T.bg));
  g_popupCardBrush = CreateSolidBrush(CR(T.card));
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
  if (g_uiFont) DeleteObject(g_uiFont);
  if (g_uiSmallFont) DeleteObject(g_uiSmallFont);
  if (g_uiBoldFont) DeleteObject(g_uiBoldFont);
  g_uiFont = CreateFontW(-SI(13), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  g_uiSmallFont = CreateFontW(-SI(11), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                              OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
  g_uiBoldFont = CreateFontW(-SI(13), 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
                             OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
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
  float uw = (308.f - 16.f) / 3.f;
  add(ID_SMART, 16, 374, uw, 56);
  add(ID_DEBLOAT, 16 + uw + 8, 374, uw, 56);
  add(ID_APPS, 16 + 2 * (uw + 8), 374, uw, 56);
  add(ID_DETAILS, kW - 16 - 12 - 64, 444, 64, 20);
  if (g_selectable) {
    for (size_t i = 0; i < g_lines.size() && i < 3; ++i) add(ID_RESULT_ROW + (int)i, 24, 466 + (float)i * 17, 280, 17);
  }
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
    struct Tile { int id; const wchar_t* icon; const wchar_t* label; } tiles[6] = {
        {ID_DUPES, L"\uE8C8", L"Duplicates"}, {ID_LEFT, L"\uE74D", L"Leftovers"}, {ID_QUAR, L"\uE72E", L"Quarantine"},
        {ID_SMART, L"\uE721", L"Advice"}, {ID_DEBLOAT, L"\uE713", L"Windows"}, {ID_APPS, L"\uE77B", L"Apps"}};
    for (auto& t : tiles) {
      RectF r = rect_of(t.id);
      bool en = enabled(t.id), hot = g_hot == t.id && en, down = g_down == t.id && hot;
      if (hot || down || !en) box(g, r, SF(8), !en ? T.track : down ? T.btnP : T.btnH, nullptr);
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
        if (g_selectable && i < g_selected.size()) {
          RectF check = R(28, y + 2, 12, 12);
          Color edge = g_selected[i] ? T.accent : T.sub;
          box(g, check, SF(3), g_selected[i] ? T.accent : T.card, &edge);
          if (g_selected[i]) txt(g, L"\uE73E", fIconS, check, T.accentText, StringAlignmentCenter);
          txt(g, g_lines[i].first, fSmall, R(46, y, 182, 17), T.text);
        } else {
          txt(g, g_lines[i].first, fSmall, R(28, y, 200, 17), T.text);
        }
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
  LONG_PTR style = GetWindowLongPtrW(g_hwnd, GWL_EXSTYLE);
  SetWindowLongPtrW(g_hwnd, GWL_EXSTYLE, style | WS_EX_LAYERED);
  SetLayeredWindowAttributes(g_hwnd, 0, 248, LWA_ALPHA);
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

static void apply_popup_chrome(HWND h) {
  BOOL dark = T.dark;
  DwmSetWindowAttribute(h, 20, &dark, sizeof dark);
  int corner = 2;
  DwmSetWindowAttribute(h, 33, &corner, sizeof corner);
  COLORREF border = CR(T.border);
  DwmSetWindowAttribute(h, 34, &border, sizeof border);
}

static void set_popup_font(HWND h, HFONT font = nullptr) {
  SendMessageW(h, WM_SETFONT, (WPARAM)(font ? font : g_uiFont), TRUE);
}

static void paint_popup_header(HWND h, const std::wstring& title, const std::wstring& subtitle) {
  PAINTSTRUCT ps;
  HDC dc = BeginPaint(h, &ps);
  RECT rc; GetClientRect(h, &rc);
  Graphics g(dc);
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
  g.Clear(T.bg);
  Pen frame(T.border, 1.0f);
  g.DrawRectangle(&frame, 0.5f, 0.5f, (REAL)max(0, rc.right - 1), (REAL)max(0, rc.bottom - 1));
  logo(g, RectF((REAL)SI(16), (REAL)SI(16), (REAL)SI(28), (REAL)SI(28)));
  txt(g, title, fTitle, RectF((REAL)SI(54), (REAL)SI(12), (REAL)max(0, rc.right - SI(122)), (REAL)SI(24)), T.text);
  txt(g, subtitle, fSmall, RectF((REAL)SI(54), (REAL)SI(37), (REAL)max(0, rc.right - SI(76)), (REAL)SI(18)), T.sub);
  Pen border(T.border, 1.0f);
  g.DrawLine(&border, 0, SI(64), rc.right, SI(64));
  g.DrawLine(&border, 0, max(SI(64), rc.bottom - SI(54)), rc.right, max(SI(64), rc.bottom - SI(54)));
  EndPaint(h, &ps);
}

enum { RV_LIST = 200, RV_ALL, RV_NONE, RV_INSPECT, RV_STAGE, RV_CLOSE, RV_SUMMARY, RV_HINT };
enum { RP_EDIT = 300, RP_COPY, RP_CLOSE, RP_SUMMARY };
enum { PP_BODY = 400, PP_CANCEL, PP_ACCEPT };
enum { PC_CLOSE = 480 };

static bool popup_primary(UINT id) { return id == RV_STAGE || id == RP_CLOSE || id == PP_ACCEPT; }

static RECT popup_close_rect(HWND h) {
  RECT rc; GetClientRect(h, &rc);
  return RECT{rc.right - SI(44), SI(12), rc.right - SI(16), SI(40)};
}

static void popup_layout_chrome(HWND h) {
  RECT r = popup_close_rect(h);
  MoveWindow(GetDlgItem(h, PC_CLOSE), r.left, r.top, r.right - r.left, r.bottom - r.top, TRUE);
}

static void create_popup_close(HWND h) {
  CreateWindowW(L"BUTTON", L"\u00d7", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
      0, 0, 0, 0, h, (HMENU)PC_CLOSE, g_inst, nullptr);
  set_popup_font(GetDlgItem(h, PC_CLOSE), g_uiFont);
  popup_layout_chrome(h);
}

static LRESULT draw_popup_button(const DRAWITEMSTRUCT* d) {
  wchar_t label[128]{};
  GetWindowTextW(d->hwndItem, label, 128);
  bool disabled = (d->itemState & ODS_DISABLED) != 0;
  bool down = (d->itemState & ODS_SELECTED) != 0;
  bool hot = (d->itemState & ODS_HOTLIGHT) != 0;
  if (d->CtlID == PC_CLOSE) {
    Graphics g(d->hDC);
    g.SetSmoothingMode(SmoothingModeAntiAlias);
    ::RECT rc = d->rcItem;
    if (down || hot) box(g, RectF((REAL)rc.left + 0.5f, (REAL)rc.top + 0.5f,
        (REAL)(rc.right - rc.left) - 1.0f, (REAL)(rc.bottom - rc.top) - 1.0f), SF(5), down ? T.btnP : T.btnH);
    txt(g, L"\u00d7", fIconS, RectF((REAL)rc.left, (REAL)rc.top, (REAL)(rc.right - rc.left),
        (REAL)(rc.bottom - rc.top)), T.sub, StringAlignmentCenter);
    return TRUE;
  }
  bool primary = popup_primary(d->CtlID);
  Color fill, text;
  Color border = T.border;
  const Color* edge = primary ? nullptr : &border;
  if (primary) {
    fill = disabled ? T.track : down ? mix(T.accent, Color(255, 0, 0, 0), 0.15f) : hot ? T.accentH : T.accent;
    text = disabled ? T.sub : T.accentText;
  } else {
    fill = disabled ? T.track : down ? T.btnP : hot ? T.btnH : T.bg;
    text = disabled ? T.sub : T.text;
  }
  Graphics g(d->hDC);
  g.SetSmoothingMode(SmoothingModeAntiAlias);
  ::RECT rc = d->rcItem;
  if (primary || disabled || down || hot) box(g, RectF((REAL)rc.left + 0.5f, (REAL)rc.top + 0.5f, (REAL)(rc.right - rc.left) - 1.0f, (REAL)(rc.bottom - rc.top) - 1.0f), SF(5), fill, edge);
  else {
    SolidBrush clear(T.bg);
    g.FillRectangle(&clear, (REAL)rc.left, (REAL)rc.top, (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top));
  }
  if (d->itemState & ODS_FOCUS) {
    Pen focus(primary ? T.accentText : T.accent, 1.0f);
    g.DrawRectangle(&focus, (REAL)rc.left + 3, (REAL)rc.top + 3, (REAL)(rc.right - rc.left) - 6, (REAL)(rc.bottom - rc.top) - 6);
  }
  txt(g, label, primary ? fBold : fBody, RectF((REAL)rc.left, (REAL)rc.top, (REAL)(rc.right - rc.left), (REAL)(rc.bottom - rc.top)), text, StringAlignmentCenter);
  return TRUE;
}

static LRESULT popup_hit_test(HWND h, LPARAM l, bool resizable) {
  POINT p{GET_X_LPARAM(l), GET_Y_LPARAM(l)};
  ScreenToClient(h, &p);
  RECT r; GetClientRect(h, &r);
  RECT close = popup_close_rect(h);
  if (PtInRect(&close, p)) return HTCLIENT;
  int edge = SI(6);
  if (resizable) {
    bool left = p.x < edge, right = p.x >= r.right - edge, top = p.y < edge, bottom = p.y >= r.bottom - edge;
    if (top && left) return HTTOPLEFT;
    if (top && right) return HTTOPRIGHT;
    if (bottom && left) return HTBOTTOMLEFT;
    if (bottom && right) return HTBOTTOMRIGHT;
    if (left) return HTLEFT;
    if (right) return HTRIGHT;
    if (top) return HTTOP;
    if (bottom) return HTBOTTOM;
  }
  return p.y < SI(64) ? HTCAPTION : HTCLIENT;
}

static void report_resize(HWND h) {
  RECT r; GetClientRect(h, &r);
  int w = r.right - r.left, hgt = r.bottom - r.top;
  MoveWindow(g_reportEdit, SI(16), SI(76), max(SI(120), w - SI(32)), max(SI(120), hgt - SI(132)), TRUE);
  int y = hgt - SI(44);
  MoveWindow(GetDlgItem(h, RP_SUMMARY), SI(18), y + SI(4), max(SI(100), w - SI(220)), SI(22), TRUE);
  MoveWindow(GetDlgItem(h, RP_COPY), max(SI(16), w - SI(184)), y, SI(80), SI(30), TRUE);
  MoveWindow(GetDlgItem(h, RP_CLOSE), max(SI(104), w - SI(96)), y, SI(80), SI(30), TRUE);
  popup_layout_chrome(h);
}

static void show_report_window(const std::string& report) {
  g_reportText = W(report);
  if (g_reportWnd) {
    SetWindowTextW(g_reportEdit, g_reportText.c_str());
    SetForegroundWindow(g_reportWnd);
    return;
  }
  g_reportWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"CnaReportWnd", L"Clean-n-Adapt | Report",
      WS_POPUP | WS_THICKFRAME | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, SI(880), SI(620), g_hwnd, nullptr, g_inst, nullptr);
  if (!g_reportWnd) set_status(L"Could not open the report window", true);
}

static void prompt_resize(HWND h) {
  RECT r; GetClientRect(h, &r);
  int w = r.right - r.left, hgt = r.bottom - r.top;
  MoveWindow(GetDlgItem(h, PP_BODY), SI(20), SI(78), max(SI(160), w - SI(40)), max(SI(48), hgt - SI(138)), TRUE);
  int y = hgt - SI(46);
  MoveWindow(GetDlgItem(h, PP_CANCEL), max(SI(20), w - SI(192)), y, SI(78), SI(30), TRUE);
  MoveWindow(GetDlgItem(h, PP_ACCEPT), max(SI(106), w - SI(104)), y, SI(84), SI(30), TRUE);
  popup_layout_chrome(h);
}

static LRESULT CALLBACK PromptWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      apply_popup_chrome(h);
      HWND body = CreateWindowW(L"STATIC", g_prompt.body.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
          0, 0, 0, 0, h, (HMENU)PP_BODY, g_inst, nullptr);
      CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
          0, 0, 0, 0, h, (HMENU)PP_CANCEL, g_inst, nullptr);
      CreateWindowW(L"BUTTON", g_prompt.action.c_str(), WS_CHILD | WS_VISIBLE | BS_OWNERDRAW,
          0, 0, 0, 0, h, (HMENU)PP_ACCEPT, g_inst, nullptr);
      set_popup_font(body, g_uiFont);
      set_popup_font(GetDlgItem(h, PP_CANCEL));
      set_popup_font(GetDlgItem(h, PP_ACCEPT), g_uiBoldFont);
      create_popup_close(h);
      prompt_resize(h);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: paint_popup_header(h, g_prompt.title, L"This action stays local and follows Clean-n-Adapt safety rules."); return 0;
    case WM_NCHITTEST: return popup_hit_test(h, l, false);
    case WM_SIZE: prompt_resize(h); return 0;
    case WM_DRAWITEM: return draw_popup_button((DRAWITEMSTRUCT*)l);
    case WM_CTLCOLORSTATIC:
      SetTextColor((HDC)w, CR(T.text)); SetBkColor((HDC)w, CR(T.card)); return (LRESULT)g_popupCardBrush;
    case WM_COMMAND:
      if (LOWORD(w) == PP_ACCEPT) { Prompt p = g_prompt; DestroyWindow(h); if (p.kind != K_NONE) start_job(p.kind, p.args, p.busy); return 0; }
      if (LOWORD(w) == PP_CANCEL || LOWORD(w) == PC_CLOSE) { DestroyWindow(h); return 0; }
      break;
    case WM_CLOSE: DestroyWindow(h); return 0;
    case WM_DESTROY: g_promptWnd = nullptr; return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

static void show_prompt(const std::wstring& title, const std::wstring& body, const std::wstring& action,
                        int kind = K_NONE, const std::wstring& args = L"", const std::wstring& busy = L"") {
  g_prompt = {title, body, action, args, busy, kind};
  if (g_promptWnd) {
    SetWindowTextW(GetDlgItem(g_promptWnd, PP_BODY), body.c_str());
    SetWindowTextW(GetDlgItem(g_promptWnd, PP_ACCEPT), action.c_str());
    SetForegroundWindow(g_promptWnd); InvalidateRect(g_promptWnd, nullptr, TRUE); return;
  }
  g_promptWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"CnaPromptWnd", L"Clean-n-Adapt | Confirm",
      WS_POPUP | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, SI(480), SI(250), g_hwnd, nullptr, g_inst, nullptr);
  if (!g_promptWnd) set_status(L"Could not open the confirmation window", true);
}

static void review_summary(HWND h) {
  uint64_t bytes = 0;
  int selected = 0, eligible = 0;
  for (size_t i = 0; i < g_reviewItems.size(); ++i) {
    if (g_reviewItems[i].eligible) ++eligible;
    if (g_reviewList && g_reviewItems[i].eligible && ListView_GetCheckState(g_reviewList, (int)i)) {
      ++selected; bytes += g_reviewItems[i].bytes;
    }
  }
  std::wstring summary = std::to_wstring(selected) + L" of " + std::to_wstring(eligible) +
      (g_reviewApplyKind == K_QAPPS ? L" official uninstallers selected" : L" eligible selected");
  if (selected && g_reviewApplyKind != K_QAPPS) summary += L"  ·  " + human(bytes);
  SetWindowTextW(GetDlgItem(h, RV_SUMMARY), summary.c_str());
}

static void review_resize(HWND h) {
  RECT r; GetClientRect(h, &r);
  int w = r.right - r.left, hgt = r.bottom - r.top;
  MoveWindow(g_reviewList, SI(16), SI(98), max(SI(160), w - SI(32)), max(SI(120), hgt - SI(160)), TRUE);
  int listW = max(SI(160), w - SI(32));
  ListView_SetColumnWidth(g_reviewList, 0, max(SI(190), listW * 30 / 100));
  ListView_SetColumnWidth(g_reviewList, 1, max(SI(100), listW * 13 / 100));
  ListView_SetColumnWidth(g_reviewList, 2, SI(86));
  ListView_SetColumnWidth(g_reviewList, 3, SI(116));
  ListView_SetColumnWidth(g_reviewList, 4, max(SI(180), listW * 31 / 100));
  int y = hgt - SI(46);
  MoveWindow(GetDlgItem(h, RV_SUMMARY), SI(18), y + SI(4), max(SI(130), w - SI(530)), SI(22), TRUE);
  MoveWindow(GetDlgItem(h, RV_ALL), max(SI(140), w - SI(500)), y, SI(86), SI(30), TRUE);
  MoveWindow(GetDlgItem(h, RV_NONE), max(SI(230), w - SI(410)), y, SI(86), SI(30), TRUE);
  MoveWindow(GetDlgItem(h, RV_INSPECT), max(SI(320), w - SI(320)), y, SI(86), SI(30), TRUE);
  MoveWindow(GetDlgItem(h, RV_STAGE), max(SI(410), w - SI(224)), y, SI(136), SI(30), TRUE);
  MoveWindow(GetDlgItem(h, RV_CLOSE), max(SI(550), w - SI(80)), y, SI(64), SI(30), TRUE);
  popup_layout_chrome(h);
}

static void stage_review_selection() {
  if (!g_reviewList) return;
  std::wstring args;
  if (g_reviewApplyKind == K_QLEFT) args = (g_reviewBaseArgs.empty() ? L"remnants" : g_reviewBaseArgs) + L" --quarantine --yes --json";
  else if (g_reviewApplyKind == K_QSMART) args = L"recommend --apply --yes --json";
  else if (g_reviewApplyKind == K_QDUPES) args = L"dupes --min-size 10M --quarantine --yes --json";
  else if (g_reviewApplyKind == K_QAPPS) args = L"apps remove --yes";
  else return;
  uint64_t bytes = 0;
  int n = 0;
  std::wstring app_label;
  for (size_t i = 0; i < g_reviewItems.size(); ++i) {
    bool checked = ListView_GetCheckState(g_reviewList, (int)i) != FALSE;
    g_reviewItems[i].selected = checked && g_reviewItems[i].eligible;
    if (!g_reviewItems[i].selected) continue;
    if (g_reviewApplyKind == K_QAPPS) {
      if (n) { show_prompt(L"Choose one app", L"Official uninstallers can be interactive. Select one app, complete its own uninstaller, then review its leftovers.", L"Close"); return; }
      args += L" --app \"" + g_reviewItems[i].command + L"\"";
      app_label = g_reviewItems[i].path;
    } else args += L" --select \"" + g_reviewItems[i].path + L"\"";
    bytes += g_reviewItems[i].bytes; ++n;
  }
  if (!n) { show_prompt(L"Nothing selected", g_reviewApplyKind == K_QAPPS ? L"Only apps with a registered official uninstaller can be selected." : L"Choose at least one eligible item before staging a quarantine. Protected items cannot be selected.", L"Close"); return; }
  g_pending = g_reviewApplyKind == K_QAPPS
      ? Pending{true, L"Uninstall " + app_label, args, K_APP_REMOVE}
      : Pending{true, L"Quarantine " + human(bytes), args, g_reviewApplyKind};
  g_confirming = false;
  set_status(g_reviewApplyKind == K_QAPPS ? L"Official uninstaller staged - click Uninstall, then Confirm" : std::to_wstring(n) + L" selected item(s) staged - click Quarantine, then Confirm");
  DestroyWindow(g_reviewWnd);
  invalidate();
}

static LRESULT CALLBACK ReviewWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      apply_popup_chrome(h);
      g_reviewList = CreateWindowExW(0, WC_LISTVIEWW, L"",
          WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS | LVS_SINGLESEL,
          0, 0, 0, 0, h, (HMENU)RV_LIST, g_inst, nullptr);
      ListView_SetExtendedListViewStyle(g_reviewList, LVS_EX_FULLROWSELECT | LVS_EX_CHECKBOXES | LVS_EX_DOUBLEBUFFER | LVS_EX_LABELTIP);
      ListView_SetBkColor(g_reviewList, CR(T.card));
      ListView_SetTextBkColor(g_reviewList, CR(T.card));
      ListView_SetTextColor(g_reviewList, CR(T.text));
      set_popup_font(g_reviewList);
      struct Col { const wchar_t* name; int width; } cols[] = {{g_reviewApplyKind == K_QAPPS ? L"Application" : L"Path", 320}, {L"Decision", 110}, {L"Size", 90}, {L"Evidence", 100}, {L"Why this result", 360}};
      for (int i = 0; i < 5; ++i) { LVCOLUMNW c{}; c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM; c.pszText = (LPWSTR)cols[i].name; c.cx = cols[i].width; c.iSubItem = i; ListView_InsertColumn(g_reviewList, i, &c); }
      for (size_t i = 0; i < g_reviewItems.size(); ++i) {
        auto& row = g_reviewItems[i];
        LVITEMW it{}; it.mask = LVIF_TEXT; it.iItem = (int)i; it.pszText = (LPWSTR)row.path.c_str();
        ListView_InsertItem(g_reviewList, &it);
        ListView_SetItemText(g_reviewList, (int)i, 1, (LPWSTR)row.category.c_str());
        std::wstring sz = human(row.bytes); ListView_SetItemText(g_reviewList, (int)i, 2, (LPWSTR)sz.c_str());
        ListView_SetItemText(g_reviewList, (int)i, 3, (LPWSTR)row.confidence.c_str());
        ListView_SetItemText(g_reviewList, (int)i, 4, (LPWSTR)row.reason.c_str());
        ListView_SetCheckState(g_reviewList, (int)i, row.selected && row.eligible);
        if (!row.eligible) ListView_SetItemState(g_reviewList, (int)i, LVIS_CUT, LVIS_CUT);
      }
      CreateWindowW(L"BUTTON", L"Select all", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, h, (HMENU)RV_ALL, g_inst, nullptr);
      CreateWindowW(L"BUTTON", L"Clear all", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, h, (HMENU)RV_NONE, g_inst, nullptr);
      CreateWindowW(L"BUTTON", L"Inspect", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, h, (HMENU)RV_INSPECT, g_inst, nullptr);
      CreateWindowW(L"BUTTON", g_reviewApplyKind == K_QAPPS ? L"Start uninstall" : L"Stage quarantine", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, h, (HMENU)RV_STAGE, g_inst, nullptr);
      CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, h, (HMENU)RV_CLOSE, g_inst, nullptr);
      HWND summary = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE | SS_LEFT, 0, 0, 0, 0, h, (HMENU)RV_SUMMARY, g_inst, nullptr);
      set_popup_font(summary, g_uiBoldFont);
      for (int id : {RV_ALL, RV_NONE, RV_INSPECT, RV_STAGE, RV_CLOSE}) set_popup_font(GetDlgItem(h, id), id == RV_STAGE ? g_uiBoldFont : g_uiFont);
      create_popup_close(h);
      review_resize(h);
      review_summary(h);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
      paint_popup_header(h, g_reviewApplyKind == K_QAPPS ? L"Installed applications" : L"Review cleanup candidates",
          g_reviewApplyKind == K_QAPPS ? L"Only registered official uninstallers may be selected. CNA never deletes an app folder directly."
                                      : L"Select eligible items, inspect the evidence, then stage a reversible quarantine.");
      return 0;
    case WM_NCHITTEST: return popup_hit_test(h, l, true);
    case WM_SIZE: review_resize(h); return 0;
    case WM_DRAWITEM: return draw_popup_button((DRAWITEMSTRUCT*)l);
    case WM_NOTIFY: {
      NMHDR* n = (NMHDR*)l;
      if (n->idFrom == RV_LIST) {
        if (n->code == LVN_ITEMCHANGING) {
          NMLISTVIEW* change = (NMLISTVIEW*)l;
          if (change->iItem >= 0 && (size_t)change->iItem < g_reviewItems.size() &&
              !g_reviewItems[(size_t)change->iItem].eligible && (change->uChanged & LVIF_STATE) &&
              ((change->uOldState ^ change->uNewState) & LVIS_STATEIMAGEMASK)) return TRUE;
        }
        if (n->code == LVN_ITEMCHANGED) review_summary(h);
        if (n->code == NM_DBLCLK) {
          int i = ListView_GetNextItem(g_reviewList, -1, LVNI_SELECTED);
          if (i >= 0 && (size_t)i < g_reviewItems.size()) start_job(K_DETAILS, L"explain \"" + g_reviewItems[(size_t)i].path + L"\"", L"Inspecting selected item...");
        }
      }
      return 0;
    }
    case WM_CTLCOLORSTATIC:
      SetTextColor((HDC)w, CR(T.sub)); SetBkColor((HDC)w, CR(T.bg)); return (LRESULT)g_popupBrush;
    case WM_COMMAND:
      switch (LOWORD(w)) {
        case RV_ALL:
          for (size_t i = 0; i < g_reviewItems.size(); ++i) if (g_reviewItems[i].eligible) ListView_SetCheckState(g_reviewList, (int)i, TRUE);
          review_summary(h);
          return 0;
        case RV_NONE:
          for (size_t i = 0; i < g_reviewItems.size(); ++i) ListView_SetCheckState(g_reviewList, (int)i, FALSE);
          review_summary(h);
          return 0;
        case RV_INSPECT: {
          int i = ListView_GetNextItem(g_reviewList, -1, LVNI_SELECTED);
          if (i >= 0 && (size_t)i < g_reviewItems.size()) start_job(K_DETAILS, L"explain \"" + g_reviewItems[(size_t)i].path + L"\"", L"Inspecting selected item...");
          return 0;
        }
        case RV_STAGE: stage_review_selection(); return 0;
        case RV_CLOSE: case PC_CLOSE: DestroyWindow(h); return 0;
      }
      break;
    case WM_DESTROY: g_reviewWnd = nullptr; g_reviewList = nullptr; return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK ReportWndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
  switch (m) {
    case WM_CREATE: {
      apply_popup_chrome(h);
      g_reportEdit = CreateWindowExW(0, L"EDIT", g_reportText.c_str(),
          WS_CHILD | WS_VISIBLE | WS_VSCROLL | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | ES_WANTRETURN,
          0, 0, 0, 0, h, (HMENU)RP_EDIT, g_inst, nullptr);
      set_popup_font(g_reportEdit, g_uiFont);
      HWND summary = CreateWindowW(L"STATIC", L"Read-only report · use Copy to share it", WS_CHILD | WS_VISIBLE | SS_LEFT,
          0, 0, 0, 0, h, (HMENU)RP_SUMMARY, g_inst, nullptr);
      CreateWindowW(L"BUTTON", L"Copy", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, h, (HMENU)RP_COPY, g_inst, nullptr);
      CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | BS_OWNERDRAW, 0, 0, 0, 0, h, (HMENU)RP_CLOSE, g_inst, nullptr);
      set_popup_font(summary, g_uiSmallFont);
      set_popup_font(GetDlgItem(h, RP_COPY));
      set_popup_font(GetDlgItem(h, RP_CLOSE), g_uiBoldFont);
      create_popup_close(h);
      report_resize(h);
      return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_PAINT:
      paint_popup_header(h, L"Report", L"A complete local record of the most recent command. No files are created.");
      return 0;
    case WM_NCHITTEST: return popup_hit_test(h, l, true);
    case WM_SIZE: report_resize(h); return 0;
    case WM_DRAWITEM: return draw_popup_button((DRAWITEMSTRUCT*)l);
    case WM_CTLCOLOREDIT:
      SetTextColor((HDC)w, CR(T.text)); SetBkColor((HDC)w, CR(T.card)); return (LRESULT)g_popupCardBrush;
    case WM_CTLCOLORSTATIC:
      SetTextColor((HDC)w, CR(T.sub)); SetBkColor((HDC)w, CR(T.bg)); return (LRESULT)g_popupBrush;
    case WM_COMMAND:
      if (LOWORD(w) == RP_COPY) { SendMessageW(g_reportEdit, EM_SETSEL, 0, -1); SendMessageW(g_reportEdit, WM_COPY, 0, 0); SetWindowTextW(GetDlgItem(h, RP_SUMMARY), L"Report copied to the clipboard"); return 0; }
      if (LOWORD(w) == RP_CLOSE || LOWORD(w) == PC_CLOSE) { DestroyWindow(h); return 0; }
      break;
    case WM_DESTROY: g_reportWnd = nullptr; g_reportEdit = nullptr; return 0;
  }
  return DefWindowProcW(h, m, w, l);
}

static void show_review_window() {
  if (g_reviewItems.empty()) return;
  if (g_reviewWnd) { SetForegroundWindow(g_reviewWnd); return; }
  g_reviewWnd = CreateWindowExW(WS_EX_TOOLWINDOW, L"CnaReviewWnd", L"Clean-n-Adapt review",
      WS_POPUP | WS_THICKFRAME | WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, SI(1100), SI(680), g_hwnd, nullptr, g_inst, nullptr);
  if (!g_reviewWnd) set_status(L"Could not open the review window", true);
}

static std::wstring prof_arg() { return kProfArgs[g_profile]; }

static void clear_result() {
  g_pending = Pending();
  g_lines.clear();
  g_heroBig = L"\u2014"; g_heroCap = L"Run a scan to see what can be cleaned"; g_heroMeta.clear();
  g_reportArgs.clear();
  g_selectable = false; g_selectPaths.clear(); g_selected.clear(); g_selectBaseArgs.clear(); g_selectKind = K_NONE; g_profileResultShown = false;
  g_reviewItems.clear(); g_reviewApplyKind = K_NONE;
  set_status(L"Ready");
}

static void save_profile_result() {
  if (!g_profileResultShown) return;
  ProfileResult& r = g_profileResults[g_profile];
  r.valid = true; r.pending = g_pending; r.status = g_status; r.heroBig = g_heroBig; r.heroCap = g_heroCap;
  r.heroMeta = g_heroMeta; r.reportArgs = g_reportArgs; r.lines = g_lines;
}

static void restore_profile_result(int profile) {
  const ProfileResult& r = g_profileResults[profile];
  if (!r.valid) { clear_result(); return; }
  g_pending = r.pending; g_status = r.status; g_heroBig = r.heroBig; g_heroCap = r.heroCap;
  g_heroMeta = r.heroMeta; g_reportArgs = r.reportArgs; g_lines = r.lines;
  g_selectable = false; g_selectPaths.clear(); g_selected.clear(); g_selectBaseArgs.clear(); g_selectKind = K_NONE;
  g_profileResultShown = true;
}

static void refresh_selection_action() {
  if (!g_selectable || g_selectKind == K_NONE) return;
  int n = 0;
  std::wstring args = g_selectBaseArgs;
  for (size_t i = 0; i < g_selected.size() && i < g_selectPaths.size(); ++i) {
    if (!g_selected[i]) continue;
    args += L" --select \"" + g_selectPaths[i] + L"\"";
    ++n;
  }
  if (!n) { g_pending = Pending(); set_status(L"Select at least one item to continue"); return; }
  g_pending = {true, L"Quarantine " + std::to_wstring(n) + (n == 1 ? L" item" : L" items"), args, g_selectKind};
}

static void on_done(JobData* j) {
  bool silent = j->silent;
  if (!silent) { g_busy = false; g_kind = K_NONE; g_confirming = false; KillTimer(g_hwnd, T_ANIM); }
  const std::string& o = j->out;
  bool json = !o.empty() && (o.find('{') != std::string::npos || j->kind == K_DEBLOAT_UNDO);
  int kind = j->kind;
  if (!silent) g_statusErr = false;
  if (kind != K_SCAN && kind != K_DETAILS && kind != K_AUTOSTAT) g_profileResultShown = false;

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
      show_report_window(o);
      set_status(L"Report opened in Clean-n-Adapt");
    }
  } else if (kind == K_APP_REMOVE) {
    if (!j->started || j->code != 0) fail(L"Official uninstaller");
    else {
      std::wstring app = g_pending.label;
      const std::wstring prefix = L"Uninstall ";
      if (app.rfind(prefix, 0) == 0) app.erase(0, prefix.size());
      g_pending = {true, L"Audit leftovers", L"apps leftovers --app \"" + app + L"\" --json", K_APP_LEFT};
      g_heroBig = L"Done"; g_heroCap = L"official uninstaller finished"; g_heroMeta.clear();
      g_reportArgs = L"apps leftovers --app \"" + app + L"\"";
      set_status(L"Nothing else was deleted - review classified leftovers next");
    }
  } else if (kind == K_APPS) {
    g_lines.clear(); g_reviewItems.clear(); g_reviewApplyKind = K_QAPPS;
    size_t p = 0; int shown = 0, total = 0, removable = 0;
    while ((p = o.find("{\"name\":\"", p)) != std::string::npos) {
      std::string name = strat(o, p + 8), publisher = strat(o, kpos(o, "publisher", p));
      std::string version = strat(o, kpos(o, "version", p));
      bool official = o.find("\"official_uninstaller\":true", p) != std::string::npos &&
          o.find("\"official_uninstaller\":true", p) < o.find('}', p);
      std::wstring evidence = official
          ? L"Registered official uninstaller"
          : L"No registered official uninstaller; protected";
      std::wstring detail = W(publisher);
      if (!version.empty()) detail += (detail.empty() ? L"" : L"  ") + W(version);
      if (detail.empty()) detail = L"Registry inventory";
      g_reviewItems.push_back({W(name), official ? L"Official uninstall" : L"Protected", evidence, detail, W(name), 0, official, false});
      if (shown < 3) { g_lines.push_back({W(name), official ? L"Official uninstall" : L"No uninstaller"}); ++shown; }
      ++total; if (official) ++removable;
      p += 9;
    }
    g_heroBig = std::to_wstring(total);
    g_heroCap = L"installed apps in the registered inventory";
    g_heroMeta = std::to_wstring(removable) + L" official uninstallers";
    g_reportArgs = L"apps list";
    if (total) { g_pending = {true, L"Review apps", L"", K_REVIEW}; set_status(L"Select one registered official uninstaller to continue"); }
    else { g_pending = Pending(); set_status(L"No registered apps found", true); }
  } else if (kind == K_APP_LEFT) {
    g_lines.clear(); g_reviewItems.clear(); g_reviewApplyKind = K_QLEFT;
    size_t p = 0; int n = 0;
    uint64_t total = 0;
    while ((p = o.find("{\"path\":\"", p)) != std::string::npos) {
      std::string path = strat(o, p + 8), category = strat(o, kpos(o, "category", p));
      std::string action = strat(o, kpos(o, "action", p)), reason = strat(o, kpos(o, "reason", p));
      uint64_t bytes = numat(o, kpos(o, "bytes", p));
      bool eligible = action == "clean";
      g_reviewItems.push_back({W(path), eligible ? L"Eligible leftover" : L"Protected / review", W(category), W(reason), L"", bytes, eligible, false});
      if (n < 3) { g_lines.push_back({leaf(path), human(bytes)}); ++n; }
      if (eligible) total += bytes;
      p += 9;
    }
    g_heroBig = human(total); g_heroCap = L"eligible leftovers after uninstall"; g_heroMeta.clear();
    g_reviewBaseArgs = g_pending.args;
    size_t json_arg = g_reviewBaseArgs.find(L" --json");
    if (json_arg != std::wstring::npos) g_reviewBaseArgs.erase(json_arg, 7);
    if (!g_reviewItems.empty()) { g_pending = {true, L"Review leftovers", L"", K_REVIEW}; set_status(L"Only evidence-backed leftovers can be selected"); }
    else { g_pending = Pending(); set_status(L"No leftovers found"); }
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
    g_selectable = false; g_selectPaths.clear(); g_selected.clear(); g_selectBaseArgs.clear(); g_selectKind = K_NONE;
    g_reviewItems.clear(); g_reviewApplyKind = K_NONE; g_profileResultShown = true;
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
    g_reviewItems.clear(); g_reviewApplyKind = K_NONE;
  g_reviewBaseArgs.clear();
    p = 0;
    while ((p = o.find("{\"size\":", p)) != std::string::npos) {
      uint64_t size = numat(o, kpos(o, "size", p));
      size_t keeper = (size_t)numat(o, kpos(o, "keeper", p));
      size_t paths = kpos(o, "paths", p), end = o.find(']', paths);
      size_t count = end == std::string::npos ? 0 : count_of(o.substr(paths, end - paths), "\",\"") + 1;
      std::string keeper_path = nth_str(o, paths, keeper);
      for (size_t i = 0; i < count; ++i) {
        std::string path = nth_str(o, paths, i);
        if (path.empty()) continue;
        bool eligible = i != keeper;
        g_reviewItems.push_back({W(path), eligible ? L"Duplicate copy" : L"Keeper", eligible ? L"Verified" : L"Protected",
                                 eligible ? L"Full SHA-256 match; keeper: " + W(keeper_path) : L"Chosen keeper is never quarantined",
                                  L"", size, eligible, eligible});
      }
      p += 8;
    }
    if (nsets) { g_reviewApplyKind = K_QDUPES; g_pending = {true, L"Review duplicates", L"", K_REVIEW}; set_status(L"Review verified copies before staging quarantine"); }
    else { g_pending = Pending(); set_status(L"No duplicates found"); }
  } else if (kind == K_LEFT) {
    size_t n = count_of(o, "\"score\":");
    uint64_t total = numat(o, kpos(o, "total_bytes"));
    g_lines.clear();
    g_selectPaths.clear(); g_selected.clear(); g_selectable = false; g_selectKind = K_NONE;
    g_reviewItems.clear(); g_reviewApplyKind = K_NONE; g_reviewBaseArgs = L"remnants";
    size_t p = 0;
    int shown = 0;
    while (shown < 3 && (p = o.find("{\"path\":\"", p)) != std::string::npos) {
      std::string path = strat(o, p + 8);
      uint64_t sc = numat(o, kpos(o, "score", p)), b = numat(o, kpos(o, "bytes", p));
      g_lines.push_back({leaf(path) + L"  [" + std::to_wstring(sc) + L"]", human(b)});
      g_selectPaths.push_back(W(path)); g_selected.push_back(true);
      p += 9; ++shown;
    }
    g_heroBig = human(total);
    g_heroCap = std::to_wstring(n) + (n == 1 ? L" folder left behind by removed apps" : L" folders left behind by removed apps");
    g_heroMeta.clear();
    g_reportArgs = L"remnants";
    if (n) {
      p = 0;
      while ((p = o.find("{\"path\":\"", p)) != std::string::npos) {
        std::string path = strat(o, p + 8);
        uint64_t sc = numat(o, kpos(o, "score", p)), b = numat(o, kpos(o, "bytes", p));
        std::string why = strat(o, kpos(o, "reason", p));
        g_reviewItems.push_back({W(path), L"Leftover", std::to_wstring(sc) + L" / 100", W(why), L"", b, true, true});
        p += 9;
      }
      g_reviewApplyKind = K_QLEFT;
      g_pending = {true, L"Review leftovers", L"", K_REVIEW};
      set_status(L"Review every candidate before staging quarantine");
    }
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
    g_lines.clear(); g_selectPaths.clear(); g_selected.clear(); g_selectable = false; g_selectKind = K_NONE;
    g_reviewItems.clear(); g_reviewApplyKind = K_NONE; g_reviewBaseArgs.clear();
    for (auto& r : clean_rows) {
      g_lines.push_back({leaf(r.path), human(r.b)}); g_selectPaths.push_back(W(r.path)); g_selected.push_back(true);
      if (g_lines.size() >= 3) break;
    }
    if (g_lines.empty()) for (auto& r : review_rows) { g_lines.push_back({leaf(r.path), human(r.b)}); if (g_lines.size() >= 3) break; }
    g_heroBig = human(clean_b);
    g_heroCap = std::to_wstring(clean_rows.size()) + (clean_rows.size() == 1 ? L" folder suggested \u00B7 " : L" folders suggested \u00B7 ") + human(review_b) + L" to review";
    g_heroMeta.clear();
    g_reportArgs = L"recommend --limit 100";
    if (!clean_rows.empty() || !review_rows.empty()) {
      p = 0;
      while ((p = o.find("{\"path\":\"", p)) != std::string::npos) {
        std::string path = strat(o, p + 8), action = strat(o, kpos(o, "action", p));
        std::string src = strat(o, kpos(o, "source", p)), note = strat(o, kpos(o, "note", p));
        std::wstring confidence = action == "clean" ? L"Eligible: " + W(src) : L"Evidence only";
        g_reviewItems.push_back({W(path), action == "clean" ? L"Eligible" : L"Needs review", confidence, W(note),
                                  L"", numat(o, kpos(o, "bytes", p)), action == "clean", action == "clean"});
        p += 9;
      }
      g_reviewApplyKind = K_QSMART;
      g_pending = {true, L"Review advice", L"", K_REVIEW};
      set_status(clean_b ? L"Review evidence before staging eligible items" : L"No item is eligible yet; inspect the evidence and teach the model if needed");
    }
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
  layout();
  delete j;
  invalidate();
}

// ---------------------------------------------------------------------------------------------
// actions
// ---------------------------------------------------------------------------------------------
static void do_click(int id) {
  if (id >= ID_PROF && id < ID_PROF + 5) {
    if (!g_busy && g_profile != id - ID_PROF) {
      save_profile_result();
      g_profile = id - ID_PROF;
      restore_profile_result(g_profile);
      layout(); save_settings();
    }
    return;
  }
  if (id >= ID_RESULT_ROW && id < ID_RESULT_ROW + 3) {
    size_t row = (size_t)(id - ID_RESULT_ROW);
    if (g_selectable && row < g_selected.size()) {
      g_selected[row] = !g_selected[row];
      refresh_selection_action();
    }
    invalidate();
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
    case ID_APPS: if (!g_busy) start_job(K_APPS, L"apps list --json", L"Reading installed apps\u2026"); break;
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
      } else if (g_pending.on && g_pending.kind == K_REVIEW) {
        show_review_window();
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
  AppendMenuW(m, MF_STRING | (g_busy ? MF_GRAYED : 0), CM_REMOVE_APPS, L"Review bloat apps\u2026");
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
          if (!g_busy) start_job(K_DEBLOAT, L"debloat scan --json", L"Reviewing bloat apps\u2026");
          break;
        case CM_UNDO_DEBLOAT:
          if (!g_busy) show_prompt(L"Undo debloat changes",
              L"Restore the recorded values for privacy tweaks, startup items, and services. Removed Microsoft Store apps are not affected.",
              L"Restore", K_DEBLOAT_UNDO, L"debloat undo --all --yes", L"Restoring settings\u2026");
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
      if (l && lstrcmpW((LPCWSTR)l, L"ImmersiveColorSet") == 0) {
        load_theme(); apply_dwm();
        if (g_reviewWnd) { apply_popup_chrome(g_reviewWnd); ListView_SetBkColor(g_reviewList, CR(T.card)); ListView_SetTextBkColor(g_reviewList, CR(T.card)); ListView_SetTextColor(g_reviewList, CR(T.text)); RedrawWindow(g_reviewWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN); }
        if (g_reportWnd) { apply_popup_chrome(g_reportWnd); RedrawWindow(g_reportWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN); }
        if (g_promptWnd) { apply_popup_chrome(g_promptWnd); RedrawWindow(g_promptWnd, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN); }
        invalidate();
      }
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
  WNDCLASSEXW review = wc;
  review.lpfnWndProc = ReviewWndProc;
  review.lpszClassName = L"CnaReviewWnd";
  review.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassExW(&review);
  WNDCLASSEXW report = wc;
  report.lpfnWndProc = ReportWndProc;
  report.lpszClassName = L"CnaReportWnd";
  report.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassExW(&report);
  WNDCLASSEXW prompt = wc;
  prompt.lpfnWndProc = PromptWndProc;
  prompt.lpszClassName = L"CnaPromptWnd";
  prompt.hCursor = LoadCursorW(nullptr, IDC_ARROW);
  RegisterClassExW(&prompt);
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
  if (g_uiFont) DeleteObject(g_uiFont);
  if (g_uiSmallFont) DeleteObject(g_uiSmallFont);
  if (g_uiBoldFont) DeleteObject(g_uiBoldFont);
  if (g_popupBrush) DeleteObject(g_popupBrush);
  if (g_popupCardBrush) DeleteObject(g_popupCardBrush);
  if (mtx) CloseHandle(mtx);
  return 0;
}
