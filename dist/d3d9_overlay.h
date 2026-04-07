#pragma once
// d3d9_overlay.h - D3D9 in-game overlay for Lirin bot
// Included directly in proxy_version.cpp; references its static globals.
// Runtime-loads D3DXCreateFontA from d3dx9_43.dll (no SDK needed).

#include <d3d9.h>
#pragma comment(lib, "d3d9.lib")

// ---------------------------------------------------------------------------
// Globals
// ---------------------------------------------------------------------------
// g_overlayVisible defined in proxy_version.cpp
static void* g_pFont          = NULL;      // ID3DXFont*

// g_btnStartStop declared in proxy_version.cpp
static BYTE  g_endSceneOrig[5] = {};
static BYTE  g_resetOrig[5]    = {};
static void* g_endSceneAddr    = NULL;
static void* g_resetAddr       = NULL;

// ---------------------------------------------------------------------------
// D3DXCreateFontA typedef (runtime loaded)
// ---------------------------------------------------------------------------
typedef int (__stdcall *pfnD3DXCreateFontA)(
    IDirect3DDevice9*, INT Height, UINT Width, UINT Weight,
    UINT MipLevels, BOOL Italic, DWORD CharSet, DWORD OutputPrecision,
    DWORD Quality, DWORD PitchAndFamily, LPCSTR pFaceName, void** ppFont);

static pfnD3DXCreateFontA s_D3DXCreateFont = NULL;

// ---------------------------------------------------------------------------
// Vertex for filled rectangles
// ---------------------------------------------------------------------------
struct OvlVertex {
    float x, y, z, rhw;
    DWORD color;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static void Ovl_DrawRect(IDirect3DDevice9* dev, float x, float y,
                         float w, float h, DWORD col) {
    OvlVertex v[4] = {
        { x,     y,     0.f, 1.f, col },
        { x + w, y,     0.f, 1.f, col },
        { x,     y + h, 0.f, 1.f, col },
        { x + w, y + h, 0.f, 1.f, col },
    };
    dev->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE);
    dev->SetTexture(0, NULL);
    dev->SetRenderState(D3DRS_ALPHABLENDENABLE, TRUE);
    dev->SetRenderState(D3DRS_SRCBLEND,  D3DBLEND_SRCALPHA);
    dev->SetRenderState(D3DRS_DESTBLEND, D3DBLEND_INVSRCALPHA);
    dev->DrawPrimitiveUP(D3DPT_TRIANGLESTRIP, 2, v, sizeof(OvlVertex));
}

// Call ID3DXFont::DrawTextA via vtable index 14
static int Ovl_DrawText(void* font, LPCSTR str, RECT* rc, DWORD fmt, DWORD col) {
    if (!font) return 0;
    typedef int (__stdcall *fnDrawText)(void*, void*, LPCSTR, INT, LPRECT, DWORD, DWORD);
    void** vtbl = *(void***)font;
    fnDrawText fn = (fnDrawText)vtbl[14];
    return fn(font, NULL, str, -1, rc, fmt, col);
}

// Call ID3DXFont::OnLostDevice (vtable 12), OnResetDevice (vtable 13), Release (vtable 2)
static void Ovl_FontOnLost(void* font) {
    if (!font) return;
    typedef void (__stdcall *fn)(void*);
    ((fn)(*(void***)font)[12])(font);
}
static void Ovl_FontOnReset(void* font) {
    if (!font) return;
    typedef void (__stdcall *fn)(void*);
    ((fn)(*(void***)font)[13])(font);
}
static void Ovl_FontRelease(void* font) {
    if (!font) return;
    typedef ULONG (__stdcall *fn)(void*);
    ((fn)(*(void***)font)[2])(font);
}

// ---------------------------------------------------------------------------
// Create font if needed
// ---------------------------------------------------------------------------
static void Ovl_EnsureFont(IDirect3DDevice9* dev) {
    if (g_pFont) return;
    if (!s_D3DXCreateFont) {
        HMODULE hd3dx = LoadLibraryA("d3dx9_43.dll");
        if (!hd3dx) return;
        s_D3DXCreateFont = (pfnD3DXCreateFontA)GetProcAddress(hd3dx, "D3DXCreateFontA");
        if (!s_D3DXCreateFont) return;
    }
    s_D3DXCreateFont(dev, 16, 0, 600, 1, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, ANTIALIASED_QUALITY,
        DEFAULT_PITCH | FF_DONTCARE, "Consolas", &g_pFont);
}

// ---------------------------------------------------------------------------
// Render the overlay
// ---------------------------------------------------------------------------
static void Ovl_Render(IDirect3DDevice9* dev) {
    if (!g_overlayVisible) return;

    Ovl_EnsureFont(dev);
    if (!g_pFont) return;

    // Save state
    IDirect3DStateBlock9* sb = NULL;
    dev->CreateStateBlock(D3DSBT_ALL, &sb);
    if (sb) sb->Capture();

    // Count stones from g_ents
    int stones = 0;
    if (g_ents) {
        for (int i = 0; i < g_nEnt && i < 512; i++)
            if (g_ents[i].isStn) stones++;
    }

    const float PW = 200.f;
    const float LX = 10.f;
    const float LY = 120.f;  // sol üst, oyun butonlarının altında
    const float RH = 22.f;                       // row height
    const DWORD BG = 0xCC000000;
    float cy = LY;                               // current y

    // --- Status: [ON]/[OFF] + Kills ---
    Ovl_DrawRect(dev, LX, cy, PW, RH, BG);

    char line[256];
    RECT rc;

    wsprintfA(line, " %s  K:%u", g_wrkOn ? "[ON]" : "[OFF]", g_killCount);
    SetRect(&rc, (int)LX, (int)cy + 2, (int)(LX + PW), (int)(cy + RH));
    Ovl_DrawText(g_pFont, line, &rc, DT_NOCLIP, g_wrkOn ? 0xFF00FF00u : 0xFFFF0000u);
    // Entire status row is clickable for toggle
    SetRect(&g_btnStartStop, (int)LX, (int)cy, (int)(LX + PW), (int)(cy + RH));
    cy += RH + 2;

    // --- GM Alert (only when flag set) ---
    if (g_alertFlag) {
        Ovl_DrawRect(dev, LX, cy, PW, RH + 2, BG);
        bool flash = (GetTickCount() % 500) < 250;
        if (flash) {
            SetRect(&rc, (int)LX, (int)cy + 2, (int)(LX + PW), (int)(cy + RH));
            Ovl_DrawText(g_pFont, "  !!! GM ALERT !!!", &rc, DT_NOCLIP, 0xFFFF0000);
        }
        cy += RH + 2;
    }

    // --- Fishing bar (only when fishing active) ---
    if (g_fishingActive) {
        Ovl_DrawRect(dev, LX, cy, PW, RH + 2, BG);

        // Build the fish bar: 30 chars wide
        // Map fishLeft..fishRight within a 0..barWidth range
        float barW   = 300.f;
        float barX0  = LX + 50.f;
        float barY0  = cy + 5.f;
        float barH   = 12.f;

        // Full bar background (dark gray)
        Ovl_DrawRect(dev, barX0, barY0, barW, barH, 0xFF333333);

        // Green zone
        float total = g_fishRight + 50.f; // approximate total range
        if (total < 1.f) total = 300.f;
        float zoneL = (g_fishLeft  / total) * barW;
        float zoneR = (g_fishRight / total) * barW;
        if (zoneR > zoneL)
            Ovl_DrawRect(dev, barX0 + zoneL, barY0, zoneR - zoneL, barH, 0xFF22C55E);

        // Cursor
        float curX = (g_fishX / total) * barW;
        bool inZone = (g_fishX >= g_fishLeft && g_fishX <= g_fishRight);
        DWORD curCol = inZone ? 0xFF22C55E : 0xFFFF4444;
        Ovl_DrawRect(dev, barX0 + curX - 2.f, barY0 - 1.f, 4.f, barH + 2.f, curCol);

        // Text: X:val [left-right]
        wsprintfA(line, " Fish:                                     X:%d [%d-%d]",
                  (int)g_fishX, (int)g_fishLeft, (int)g_fishRight);
        SetRect(&rc, (int)LX, (int)cy + 2, (int)(LX + PW), (int)(cy + RH));
        Ovl_DrawText(g_pFont, line, &rc, DT_NOCLIP, 0xFFE0E0E0);
        cy += RH + 2;
    }

    // No hints row - clean UI

    // Restore state
    if (sb) { sb->Apply(); sb->Release(); }
}

// ---------------------------------------------------------------------------
// Click handler - returns true if click was consumed by overlay
// ---------------------------------------------------------------------------
static bool Ovl_OnClick(int x, int y) {
    if (!g_overlayVisible) return false;
    POINT pt = {x, y};
    if (PtInRect(&g_btnStartStop, pt)) {
        g_wrkOn = !g_wrkOn;
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// EndScene hook
// ---------------------------------------------------------------------------
typedef HRESULT (__stdcall *tEndScene)(IDirect3DDevice9*);
static tEndScene g_oEndScene = NULL;

static void Ovl_Unpatch(void* addr, BYTE* orig) {
    DWORD old;
    VirtualProtect(addr, 5, PAGE_EXECUTE_READWRITE, &old);
    memcpy(addr, orig, 5);
    VirtualProtect(addr, 5, old, &old);
}
static void Ovl_Patch(void* addr, void* dst, BYTE* origOut) {
    DWORD old;
    VirtualProtect(addr, 5, PAGE_EXECUTE_READWRITE, &old);
    if (origOut) memcpy(origOut, addr, 5);
    *(BYTE*)addr = 0xE9;
    *(DWORD*)((BYTE*)addr + 1) = (DWORD)((BYTE*)dst - (BYTE*)addr - 5);
    VirtualProtect(addr, 5, old, &old);
}

static HRESULT __stdcall hkEndScene(IDirect3DDevice9* dev) {
    // Temporarily restore original bytes so we can call the real function
    Ovl_Unpatch(g_endSceneAddr, g_endSceneOrig);

    __try {
        Ovl_Render(dev);
    } __except (EXCEPTION_EXECUTE_HANDLER) {}

    HRESULT hr = dev->EndScene();

    // Re-apply hook
    Ovl_Patch(g_endSceneAddr, &hkEndScene, NULL);
    return hr;
}

// ---------------------------------------------------------------------------
// Reset hook - release / recreate font
// ---------------------------------------------------------------------------
typedef HRESULT (__stdcall *tReset)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
static tReset g_oReset = NULL;

static HRESULT __stdcall hkReset(IDirect3DDevice9* dev, D3DPRESENT_PARAMETERS* pp) {
    Ovl_FontOnLost(g_pFont);

    Ovl_Unpatch(g_resetAddr, g_resetOrig);
    HRESULT hr = dev->Reset(pp);
    Ovl_Patch(g_resetAddr, &hkReset, NULL);

    if (SUCCEEDED(hr))
        Ovl_FontOnReset(g_pFont);
    else {
        // Font is toast, release and recreate next frame
        Ovl_FontRelease(g_pFont);
        g_pFont = NULL;
    }
    return hr;
}

// ---------------------------------------------------------------------------
// Installation: create dummy device, grab vtable, hook
// ---------------------------------------------------------------------------
static void Overlay_Install() {
    // Need a window - use game hwnd if available, else desktop
    HWND hwnd = g_gameHwnd;
    if (!hwnd) hwnd = GetDesktopWindow();

    IDirect3D9* d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) return;

    D3DPRESENT_PARAMETERS pp = {};
    pp.Windowed           = TRUE;
    pp.SwapEffect         = D3DSWAPEFFECT_DISCARD;
    pp.hDeviceWindow      = hwnd;
    pp.BackBufferFormat   = D3DFMT_UNKNOWN;

    IDirect3DDevice9* dummy = NULL;
    HRESULT hr = d3d->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_NULLREF,
                                   hwnd, D3DCREATE_SOFTWARE_VERTEXPROCESSING,
                                   &pp, &dummy);
    if (FAILED(hr) || !dummy) {
        d3d->Release();
        return;
    }

    // Grab vtable pointers
    void** vtbl = *(void***)dummy;
    g_endSceneAddr = vtbl[42];
    g_resetAddr    = vtbl[16];

    dummy->Release();
    d3d->Release();

    // Hook EndScene
    Ovl_Patch(g_endSceneAddr, &hkEndScene, g_endSceneOrig);

    // Hook Reset
    Ovl_Patch(g_resetAddr, &hkReset, g_resetOrig);
}
