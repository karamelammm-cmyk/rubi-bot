#pragma once
// GDI overlay - transparent layered window on top of game
// No D3D9 hooks, no multi-client conflicts

static bool g_overlayNeedsInstall = false;
static HWND g_overlayWnd = NULL;
static HANDLE g_overlayThread = NULL;

// g_overlayVisible, g_btnStartStop, g_btnFish declared in proxy_version.cpp

static void Overlay_TryHook() {} // unused

// Paint the overlay content
static void Ovl_Paint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);

    // Get game window position to align
    RECT gameRc;
    if (g_gameHwnd) GetWindowRect(g_gameHwnd, &gameRc);

    // Double buffer
    RECT cr; GetClientRect(hwnd, &cr);
    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, cr.right, cr.bottom);
    SelectObject(mem, bmp);

    // Transparent background
    HBRUSH bgBrush = CreateSolidBrush(RGB(1,1,1)); // color key
    FillRect(mem, &cr, bgBrush);
    DeleteObject(bgBrush);

    // Semi-transparent panel area
    int px = 10, py = 10, pw = 200, rh = 22;

    // Count stones, players, GMs + queue names
    int stones = 0, playerCount = 0, gmCount = 0;
    char playerNames[8][48] = {};
    char gmNames[4][48] = {};
    char queueNames[16][48] = {};
    if (g_ents) {
        for (int i = 0; i < g_nEnt && i < 512; i++) {
            if (g_ents[i].isStn) stones++;
            if (g_ents[i].vid == g_playerVid) continue;
            if (IsGMEntity(g_ents[i]) && gmCount < 4) {
                lstrcpynA(gmNames[gmCount], g_ents[i].name, 48);
                gmCount++;
            } else if (g_ents[i].charType == 2 && !g_ents[i].isStn && playerCount < 8) {
                lstrcpynA(playerNames[playerCount], g_ents[i].name, 48);
                playerCount++;
            }
        }
        // Queue'daki metinlerin isimlerini bul
        for (int q = 0; q < g_queueCount && q < 16; q++) {
            queueNames[q][0] = 0;
            for (int i = 0; i < g_nEnt && i < 512; i++) {
                if (g_ents[i].vid == g_queuedVids[q]) {
                    lstrcpynA(queueNames[q], g_ents[i].name, 48);
                    break;
                }
            }
            if (queueNames[q][0] == 0)
                wsprintfA(queueNames[q], "VID %u", g_queuedVids[q]);
        }
    }

    // Calculate panel height
    int rows = 4; // ON/OFF, Fish, Pickup, Stats
    if (g_fishingActive) rows++; // Anim Skip
    if (g_alertFlag || gmCount > 0) rows++; // GM alert line
    for (int g = 0; g < gmCount; g++) rows++; // GM names
    if (g_queueCount > 0) rows++; // "Queue:" header
    for (int q = 0; q < g_queueCount && q < 16; q++) rows++; // Queue items
    if (playerCount > 0) rows++; // "Players:" header
    for (int p = 0; p < playerCount; p++) rows++; // Player names

    HBRUSH panelBrush = CreateSolidBrush(RGB(20,20,30));
    RECT panelRc = {px, py, px+pw, py + rh*rows + 8};

    // GM alert extends panel
    if (g_alertFlag) panelRc.bottom += rh + 2;
    // Fishing gauge extends panel
    if (g_fishingActive && g_fishX > 5) panelRc.bottom += rh + 2;

    FillRect(mem, &panelRc, panelBrush);
    DeleteObject(panelBrush);

    SetBkMode(mem, TRANSPARENT);
    HFONT font = CreateFontA(16, 0, 0, 0, FW_BOLD, 0, 0, 0, DEFAULT_CHARSET,
        0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
    SelectObject(mem, font);

    int cy = py + 2;

    // Row 1: [ON]/[OFF] + Kills
    char line[256];
    if (g_fishingActive) {
        wsprintfA(line, " FISH  K:%u", g_killCount);
        SetTextColor(mem, RGB(56, 189, 248));
    } else {
        wsprintfA(line, " %s  K:%u", g_wrkOn ? "[ON]" : "[OFF]", g_killCount);
        SetTextColor(mem, g_wrkOn ? RGB(0,255,0) : RGB(255,68,68));
    }
    RECT rc = {px, cy, px+pw, cy+rh};
    DrawTextA(mem, line, -1, &rc, DT_LEFT | DT_NOCLIP);
    SetRect(&g_btnStartStop, px, cy, px+pw, cy+rh);
    cy += rh;

    // Row 2: Fish toggle
    wsprintfA(line, " Fish: %s", g_fishingActive ? "[ON]" : "[OFF]");
    SetTextColor(mem, g_fishingActive ? RGB(56, 189, 248) : RGB(102,102,102));
    rc = {px, cy, px+pw, cy+rh};
    DrawTextA(mem, line, -1, &rc, DT_LEFT | DT_NOCLIP);
    SetRect(&g_btnFish, px, cy, px+pw, cy+rh);
    cy += rh;

    // Row 3: Anim Skip (only when fishing active)
    if (g_fishingActive) {
        wsprintfA(line, " Anim Skip: %s", g_fishAnimSkip ? "[ON]" : "[OFF]");
        SetTextColor(mem, g_fishAnimSkip ? RGB(56, 248, 120) : RGB(102,102,102));
        rc = {px, cy, px+pw, cy+rh};
        DrawTextA(mem, line, -1, &rc, DT_LEFT | DT_NOCLIP);
        SetRect(&g_btnAnimSkip, px, cy, px+pw, cy+rh);
        cy += rh;
    } else {
        SetRect(&g_btnAnimSkip, 0, 0, 0, 0);
    }

    // Row 4: Pickup toggle
    wsprintfA(line, " Pickup: %s", g_pickupEnabled ? "[ON]" : "[OFF]");
    SetTextColor(mem, g_pickupEnabled ? RGB(0, 230, 118) : RGB(102,102,102));
    rc = {px, cy, px+pw, cy+rh};
    DrawTextA(mem, line, -1, &rc, DT_LEFT | DT_NOCLIP);
    SetRect(&g_btnPickup, px, cy, px+pw, cy+rh);
    cy += rh;

    // Row 5: Stones / Entities / Queue
    wsprintfA(line, " S:%d  E:%d  Q:%d", stones, g_nEnt, g_queueCount);
    SetTextColor(mem, RGB(180,180,180));
    rc = {px, cy, px+pw, cy+rh};
    DrawTextA(mem, line, -1, &rc, DT_LEFT | DT_NOCLIP);
    cy += rh + 4;

    // Queue list
    if (g_queueCount > 0) {
        wsprintfA(line, " Queue: %d", g_queueCount);
        SetTextColor(mem, RGB(255, 145, 0));
        rc = {px, cy, px+pw, cy+rh};
        DrawTextA(mem, line, -1, &rc, DT_LEFT | DT_NOCLIP);
        cy += rh;
        HFONT smallFont = CreateFontA(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
        HFONT oldFont = (HFONT)SelectObject(mem, smallFont);
        for (int q = 0; q < g_queueCount && q < 16; q++) {
            wsprintfA(line, "  %d. %s", q+1, queueNames[q]);
            SetTextColor(mem, (q == 0) ? RGB(224, 64, 251) : RGB(255, 180, 80));
            rc = {px, cy, px+pw, cy+rh-4};
            DrawTextA(mem, line, -1, &rc, DT_LEFT | DT_NOCLIP);
            cy += rh - 4;
        }
        SelectObject(mem, oldFont);
        DeleteObject(smallFont);
        cy += 2;
    }

    // GM Alert + GM names
    if (g_alertFlag || gmCount > 0) {
        bool flash = (GetTickCount() % 500) < 250;
        SetTextColor(mem, flash ? RGB(255,0,0) : RGB(255,200,0));
        rc = {px, cy, px+pw, cy+rh};
        DrawTextA(mem, " !!! GM ALERT !!!", -1, &rc, DT_LEFT | DT_NOCLIP);
        cy += rh;
        // GM names
        HFONT smallFont = CreateFontA(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
        HFONT oldFont = (HFONT)SelectObject(mem, smallFont);
        for (int g = 0; g < gmCount; g++) {
            wsprintfA(line, "  ! %s", gmNames[g]);
            SetTextColor(mem, RGB(255, 80, 80));
            rc = {px, cy, px+pw, cy+rh-4};
            DrawTextA(mem, line, -1, &rc, DT_LEFT | DT_NOCLIP);
            cy += rh - 4;
        }
        SelectObject(mem, oldFont);
        DeleteObject(smallFont);
        cy += 2;
    }

    // Player list
    if (playerCount > 0) {
        wsprintfA(line, " Players: %d", playerCount);
        SetTextColor(mem, RGB(68, 138, 255));
        rc = {px, cy, px+pw, cy+rh};
        DrawTextA(mem, line, -1, &rc, DT_LEFT | DT_NOCLIP);
        cy += rh;
        HFONT smallFont = CreateFontA(13, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
            0, 0, CLEARTYPE_QUALITY, FIXED_PITCH, "Consolas");
        HFONT oldFont = (HFONT)SelectObject(mem, smallFont);
        for (int p = 0; p < playerCount; p++) {
            wsprintfA(line, "  - %s", playerNames[p]);
            SetTextColor(mem, RGB(120, 170, 255));
            rc = {px, cy, px+pw, cy+rh-4};
            DrawTextA(mem, line, -1, &rc, DT_LEFT | DT_NOCLIP);
            cy += rh - 4;
        }
        SelectObject(mem, oldFont);
        DeleteObject(smallFont);
        cy += 2;
    }

    // Fishing gauge
    if (g_fishingActive && g_fishX > 5) {
        float total = 500.f;
        int barW = pw - 20, barX = px + 10;
        int barY = cy + 4, barH = 14;

        // Bar background
        HBRUSH barBg = CreateSolidBrush(RGB(50,50,50));
        RECT barRc = {barX, barY, barX+barW, barY+barH};
        FillRect(mem, &barRc, barBg);
        DeleteObject(barBg);

        // Green zone
        int zL = (int)(g_fishLeft / total * barW);
        int zR = (int)(g_fishRight / total * barW);
        if (zR > zL) {
            HBRUSH greenBr = CreateSolidBrush(RGB(34,197,94));
            RECT gRc = {barX+zL, barY, barX+zR, barY+barH};
            FillRect(mem, &gRc, greenBr);
            DeleteObject(greenBr);
        }

        // Cursor
        int curX = (int)(g_fishX / total * barW);
        bool inZone = (g_fishX >= g_fishLeft && g_fishX <= g_fishRight);
        HBRUSH curBr = CreateSolidBrush(inZone ? RGB(34,197,94) : RGB(255,68,68));
        RECT cRc = {barX+curX-2, barY-1, barX+curX+2, barY+barH+1};
        FillRect(mem, &cRc, curBr);
        DeleteObject(curBr);

        cy += rh + 2;
    }

    DeleteObject(font);

    // Copy to window
    BitBlt(hdc, 0, 0, cr.right, cr.bottom, mem, 0, 0, SRCCOPY);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK OvlWndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_ERASEBKGND) return 1; // prevent flicker
    if (msg == WM_PAINT) {
        Ovl_Paint(hwnd);
        return 0;
    }
    if (msg == WM_LBUTTONDOWN) {
        int mx = (short)LOWORD(lp), my = (short)HIWORD(lp);
        POINT pt = {mx, my};
        if (PtInRect(&g_btnStartStop, pt)) { g_wrkOn = !g_wrkOn; InvalidateRect(hwnd, NULL, FALSE); return 0; }
        if (PtInRect(&g_btnFish, pt)) {
            g_fishingActive = !g_fishingActive;
            if (!g_fishingActive) {
                g_fishScanned = false; g_fishScanX = 0; g_fishLoopState = 0;
                g_fishCatchSent = false; g_fishX = 0; g_fishLeft = 0; g_fishRight = 0;
            }
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (PtInRect(&g_btnAnimSkip, pt)) {
            g_fishAnimSkip = !g_fishAnimSkip;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
        if (PtInRect(&g_btnPickup, pt)) {
            g_pickupEnabled = !g_pickupEnabled;
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        }
    }
    if (msg == WM_DESTROY) { g_overlayWnd = NULL; return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static DWORD WINAPI OverlayThreadProc(LPVOID) {
    // Wait for game window
    while (!g_gameHwnd) Sleep(500);
    Sleep(2000); // let game fully load

    WNDCLASSA wc = {};
    wc.lpfnWndProc = OvlWndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "OvlCls";
    wc.hCursor = LoadCursorA(NULL, IDC_ARROW);
    RegisterClassA(&wc);

    // Create layered transparent window
    g_overlayWnd = CreateWindowExA(
        WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        "OvlCls", "", WS_POPUP | WS_VISIBLE,
        0, 0, 220, 200, NULL, NULL, wc.hInstance, NULL);

    // Color key: RGB(1,1,1) = transparent
    SetLayeredWindowAttributes(g_overlayWnd, RGB(1,1,1), 0, LWA_COLORKEY);

    // Position over game window
    RECT gr;
    if (g_gameHwnd && GetWindowRect(g_gameHwnd, &gr)) {
        SetWindowPos(g_overlayWnd, HWND_TOPMOST, gr.left + 10, gr.top + 50, 220, 200, SWP_NOACTIVATE);
    }

    // Message loop + periodic repaint
    DWORD lastPaint = 0;
    MSG msg;
    while (g_overlayWnd) {
        while (PeekMessageA(&msg, g_overlayWnd, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }

        // Update every 200ms
        if (GetTickCount() - lastPaint > 200) {
            lastPaint = GetTickCount();
            if (g_overlayWnd && g_gameHwnd) {
                if (GetWindowRect(g_gameHwnd, &gr)) {
                    int h = 100; // base (ON/OFF, Fish, Pickup, Stats)
                    if (g_fishingActive) h += 22; // Anim Skip row
                    if (g_alertFlag) h += 24; // GM alert text
                    if (g_fishingActive && g_fishX > 5) h += 24; // Fish gauge
                    // Count players/GMs for height
                    int pCnt = 0, gCnt = 0;
                    for (int i = 0; i < g_nEnt && i < 512; i++) {
                        if (g_ents[i].vid == g_playerVid) continue;
                        if (IsGMEntity(g_ents[i]) && gCnt < 4) gCnt++;
                        else if (g_ents[i].charType == 2 && !g_ents[i].isStn && pCnt < 8) pCnt++;
                    }
                    if (g_queueCount > 0) h += 22 + g_queueCount * 18; // Queue header + items
                    if (g_alertFlag || gCnt > 0) h += 22 + gCnt * 18; // GM header + names
                    if (pCnt > 0) h += 22 + pCnt * 18; // Player header + names
                    SetWindowPos(g_overlayWnd, HWND_TOPMOST, gr.left + 10, gr.top + 50, 220, h,
                        SWP_NOACTIVATE | (g_overlayVisible ? SWP_SHOWWINDOW : SWP_HIDEWINDOW));
                }
                InvalidateRect(g_overlayWnd, NULL, FALSE);
                UpdateWindow(g_overlayWnd);
            }
        }
        Sleep(30);
    }
    return 0;
}

static void Overlay_Install() {
    if (g_overlayThread) return;
    g_overlayThread = CreateThread(NULL, 0, OverlayThreadProc, NULL, 0, NULL);
}
