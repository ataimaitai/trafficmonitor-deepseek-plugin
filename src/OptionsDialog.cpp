/*********************************************************
* DeepSeekPlugin - 选项设置对话框 (纯 Win32 API, 无资源文件)
* API Key 不在本对话框中输入/保存, 仅配置凭据文件路径,
* Key 从凭据文件(如 ~/.dsh/.credentials.yaml)的 refs: DEEPSEEK_API_KEY 读取。
**********************************************************/
#include "DeepSeekPlugin.h"
#include <commdlg.h>      // GetOpenFileNameW (WIN32_LEAN_AND_MEAN 下需显式包含)

namespace {

const wchar_t* DLG_CLASS = L"DSPluginOptionsWnd";
const int ID_EDIT_CRED   = 101;
const int ID_EDIT_MIN    = 102;
const int ID_EDIT_PROXY  = 103;
const int ID_COMBO_MODE  = 104;
const int ID_BTN_TEST    = 105;
const int ID_BTN_BROWSE  = 106;

struct OptCtx
{
    HWND hWnd = nullptr;
    HWND hCred = nullptr, hMin = nullptr, hProxy = nullptr, hCombo = nullptr, hNote = nullptr;
    bool changed = false;
    bool closed = false;
};

HFONT g_hFont = nullptr;

BOOL CALLBACK SetChildFont(HWND hChild, LPARAM lParam)
{
    SendMessageW(hChild, WM_SETFONT, (WPARAM)lParam, TRUE);
    return TRUE;
}

std::wstring GetEditText(HWND hEdit)
{
    int len = GetWindowTextLengthW(hEdit);
    std::wstring s(len + 1, 0);
    GetWindowTextW(hEdit, &s[0], len + 1);
    s.resize(len);
    return s;
}

// 默认凭据文件路径使用 Credentials.cpp 中的 DefaultCredentialPath()

void BrowseCredentialFile(HWND hWnd, OptCtx* ctx)
{
    wchar_t szFile[MAX_PATH] = {};
    std::wstring cur = GetEditText(ctx->hCred);
    if (!cur.empty() && cur.size() < MAX_PATH)
        wcscpy_s(szFile, cur.c_str());
    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hWnd;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrFilter = L"凭据文件 (*.yaml;*.yml)\0*.yaml;*.yml\0所有文件\0*.*\0";
    ofn.lpstrTitle = L"选择凭据文件";
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
    if (GetOpenFileNameW(&ofn))
        SetWindowTextW(ctx->hCred, szFile);
}

void DoTest(HWND hWnd, OptCtx* ctx)
{
    DSConfig cfg;
    cfg.credentialFile = GetEditText(ctx->hCred);
    cfg.proxyServer = GetEditText(ctx->hProxy);
    cfg.proxyMode = (int)SendMessageW(ctx->hCombo, CB_GETCURSEL, 0, 0);
    if (cfg.proxyMode < 0 || cfg.proxyMode > 2) cfg.proxyMode = 2;

    ResolvedKey rk = ResolveApiKey(cfg);
    if (rk.key.empty())
    {
        MessageBoxW(hWnd, (L"无法获取 API Key：\n" + rk.error).c_str(),
                    L"测试连接", MB_OK | MB_ICONWARNING);
        return;
    }

    HCURSOR hWait = LoadCursorW(nullptr, IDC_WAIT);
    SetCursor(hWait);
    EnableWindow(hWnd, FALSE);

    std::vector<std::wstring> headers;
    headers.push_back(L"Authorization: Bearer " + rk.key);
    headers.push_back(L"Accept: application/json");
    HttpResult r = HttpGetAuto(L"api.deepseek.com", 443, L"/user/balance", headers, cfg);

    EnableWindow(hWnd, TRUE);

    std::wstring msg;
    if (!r.networkOk)
        msg = L"网络请求失败：\n" + r.error;
    else if (r.status == 200)
    {
        std::string s;
        bool avail = false;
        JsonFindBool(r.body, "is_available", avail);
        std::string total, cur;
        JsonFindString(r.body, "total_balance", total);
        JsonFindString(r.body, "currency", cur);
        msg = L"连接成功！\n\n账户余额: " + std::wstring(cur == "USD" ? L"$" : L"￥") + Utf8ToWide(total)
            + (avail ? L"（可用）" : L"（不可用）")
            + L"\nKey 来源: " + rk.source
            + (r.usedProxy ? L"\n(通过代理)" : L"\n(直连)");
    }
    else if (r.status == 401)
        msg = L"HTTP 401：凭据文件中的 DEEPSEEK_API_KEY 无效或已撤销。\n来源: " + rk.source;
    else
        msg = L"服务器返回 HTTP " + std::to_wstring(r.status) + L"。";

    MessageBoxW(hWnd, msg.c_str(), L"测试连接", MB_OK | (r.networkOk && r.status == 200 ? MB_ICONINFORMATION : MB_ICONWARNING));
}

LRESULT CALLBACK OptWndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    OptCtx* ctx = (OptCtx*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
    switch (msg)
    {
    case WM_COMMAND:
    {
        int id = LOWORD(wParam);
        if (id == ID_BTN_BROWSE)
        {
            BrowseCredentialFile(hWnd, ctx);
            return 0;
        }
        if (id == ID_BTN_TEST)
        {
            DoTest(hWnd, ctx);
            return 0;
        }
        if (id == IDOK)
        {
            DSConfig cfg;
            cfg.credentialFile = GetEditText(ctx->hCred);
            cfg.proxyServer = GetEditText(ctx->hProxy);
            cfg.proxyMode = (int)SendMessageW(ctx->hCombo, CB_GETCURSEL, 0, 0);
            if (cfg.proxyMode < 0 || cfg.proxyMode > 2) cfg.proxyMode = 2;
            std::wstring minText = GetEditText(ctx->hMin);
            int mins = _wtoi(minText.c_str());
            if (mins < 1 || mins > 1440)
            {
                MessageBoxW(hWnd, L"刷新间隔应为 1 ~ 1440 之间的分钟数。", L"提示", MB_OK | MB_ICONWARNING);
                return 0;
            }
            cfg.refreshMinutes = mins;
            // 安全: 配置了凭据文件后, 清除配置文件中遗留的明文 Key
            if (!cfg.credentialFile.empty())
                cfg.apiKey.clear();
            else
            {
                EnterCriticalSection(&g_cs);
                cfg.apiKey = g_config.apiKey;   // 保留旧版明文 Key 兜底(不新增)
                LeaveCriticalSection(&g_cs);
            }

            EnterCriticalSection(&g_cs);
            g_config = cfg;
            SaveConfigToIni();
            LeaveCriticalSection(&g_cs);
            TriggerDeepSeekRefresh();

            ctx->changed = true;
            DestroyWindow(hWnd);
            return 0;
        }
        if (id == IDCANCEL)
        {
            DestroyWindow(hWnd);
            return 0;
        }
        break;
    }
    case WM_CLOSE:
        DestroyWindow(hWnd);
        return 0;
    case WM_DESTROY:
        ctx->closed = true;
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

HWND MakeStatic(HWND parent, const wchar_t* text, int x, int y, int w, int h)
{
    return CreateWindowExW(0, L"STATIC", text, WS_CHILD | WS_VISIBLE | SS_LEFT,
                           x, y, w, h, parent, nullptr, g_hInst, nullptr);
}

} // namespace

bool ShowDSOptionsDialog(HWND hParent)
{
    // 注册窗口类(一次)
    static bool classRegistered = false;
    if (!classRegistered)
    {
        WNDCLASSW wc = {};
        wc.lpfnWndProc = OptWndProc;
        wc.hInstance = g_hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
        wc.lpszClassName = DLG_CLASS;
        if (!RegisterClassW(&wc))
            return false;
        classRegistered = true;
    }
    if (!g_hFont)
    {
        g_hFont = CreateFontW(-14, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    }

    // 快照当前配置
    EnterCriticalSection(&g_cs);
    DSConfig cfg = g_config;
    LeaveCriticalSection(&g_cs);

    // 未配置时预填默认凭据文件路径(仅当文件存在)
    std::wstring credInit = cfg.credentialFile;
    if (credInit.empty())
    {
        std::wstring def = DefaultCredentialPath();
        if (!def.empty() && GetFileAttributesW(def.c_str()) != INVALID_FILE_ATTRIBUTES)
            credInit = def;
    }

    const int W = 575, H = 218;
    int px = CW_USEDEFAULT, py = CW_USEDEFAULT;
    if (hParent && IsWindow(hParent))
    {
        RECT rc;
        GetWindowRect(hParent, &rc);
        px = rc.left + ((rc.right - rc.left) - W) / 2;
        py = rc.top + ((rc.bottom - rc.top) - H) / 2;
        if (px < 0) px = 0;
        if (py < 0) py = 0;
    }

    OptCtx ctx;
    ctx.hWnd = CreateWindowExW(WS_EX_DLGMODALFRAME, DLG_CLASS, L"DeepSeek 插件选项",
                               WS_POPUP | WS_CAPTION | WS_SYSMENU,
                               px, py, W, H, hParent, nullptr, g_hInst, nullptr);
    if (!ctx.hWnd)
        return false;
    SetWindowLongPtrW(ctx.hWnd, GWLP_USERDATA, (LONG_PTR)&ctx);

    int y = 15;
    MakeStatic(ctx.hWnd, L"凭据文件:", 15, y + 3, 70, 20);
    ctx.hCred = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", credInit.c_str(),
                                WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                88, y, 372, 24, ctx.hWnd, (HMENU)(INT_PTR)ID_EDIT_CRED, g_hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"浏览…", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    468, y - 1, 72, 26, ctx.hWnd, (HMENU)(INT_PTR)ID_BTN_BROWSE, g_hInst, nullptr);
    y += 36;
    MakeStatic(ctx.hWnd, L"刷新间隔(分钟):", 15, y + 3, 105, 20);
    wchar_t numbuf[16];
    swprintf(numbuf, 16, L"%d", cfg.refreshMinutes);
    ctx.hMin = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", numbuf,
                               WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
                               122, y, 48, 24, ctx.hWnd, (HMENU)(INT_PTR)ID_EDIT_MIN, g_hInst, nullptr);
    MakeStatic(ctx.hWnd, L"代理服务器:", 190, y + 3, 78, 20);
    ctx.hProxy = CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", cfg.proxyServer.c_str(),
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
                                 270, y, 115, 24, ctx.hWnd, (HMENU)(INT_PTR)ID_EDIT_PROXY, g_hInst, nullptr);
    MakeStatic(ctx.hWnd, L"代理方式:", 400, y + 3, 62, 20);
    ctx.hCombo = CreateWindowExW(0, L"COMBOBOX", nullptr,
                                 WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                 462, y, 98, 240, ctx.hWnd, (HMENU)(INT_PTR)ID_COMBO_MODE, g_hInst, nullptr);
    SendMessageW(ctx.hCombo, CB_ADDSTRING, 0, (LPARAM)L"仅直连");
    SendMessageW(ctx.hCombo, CB_ADDSTRING, 0, (LPARAM)L"仅代理");
    SendMessageW(ctx.hCombo, CB_ADDSTRING, 0, (LPARAM)L"自动回退");
    SendMessageW(ctx.hCombo, CB_SETCURSEL, (WPARAM)(cfg.proxyMode >= 0 && cfg.proxyMode <= 2 ? cfg.proxyMode : 2), 0);

    y += 36;
    std::wstring note =
        L"API Key 从上方凭据文件的 refs: DEEPSEEK_API_KEY 读取，插件不保存 Key 明文。";
    ctx.hNote = CreateWindowExW(0, L"STATIC", note.c_str(), WS_CHILD | WS_VISIBLE | SS_LEFT,
                                15, y, 545, 40, ctx.hWnd, nullptr, g_hInst, nullptr);

    y += 46;
    CreateWindowExW(0, L"BUTTON", L"测试连接", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    15, y, 95, 30, ctx.hWnd, (HMENU)(INT_PTR)ID_BTN_TEST, g_hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"确定", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
                    355, y, 90, 30, ctx.hWnd, (HMENU)(INT_PTR)IDOK, g_hInst, nullptr);
    CreateWindowExW(0, L"BUTTON", L"取消", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_PUSHBUTTON,
                    455, y, 90, 30, ctx.hWnd, (HMENU)(INT_PTR)IDCANCEL, g_hInst, nullptr);

    EnumChildWindows(ctx.hWnd, SetChildFont, (LPARAM)g_hFont);

    if (hParent && IsWindow(hParent))
        EnableWindow(hParent, FALSE);
    ShowWindow(ctx.hWnd, SW_SHOW);
    UpdateWindow(ctx.hWnd);
    SetFocus(ctx.hCred);

    // 局部消息循环(模态)
    MSG m;
    while (!ctx.closed)
    {
        BOOL ret = GetMessageW(&m, nullptr, 0, 0);
        if (ret == 0 || ret == -1)        // WM_QUIT: 重新投递给主程序消息循环
        {
            if (ret == 0) PostQuitMessage((int)m.wParam);
            break;
        }
        if (!IsDialogMessageW(ctx.hWnd, &m))
        {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }

    if (hParent && IsWindow(hParent))
    {
        EnableWindow(hParent, TRUE);
        SetForegroundWindow(hParent);
    }
    return ctx.changed;
}
