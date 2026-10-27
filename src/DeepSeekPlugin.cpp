/*********************************************************
* DeepSeekPlugin - 插件主体实现
**********************************************************/
#include "DeepSeekPlugin.h"

// ---------------------------------------------------------------- 全局定义
HINSTANCE g_hInst = nullptr;
CRITICAL_SECTION g_cs;
DSConfig g_config;
BalanceState g_balance;
std::wstring g_configDir;
HolidayData g_holidays;

std::atomic<bool> g_stop{ false };
std::atomic<bool> g_forceRefresh{ false };
static HANDLE g_hWake = nullptr;
static LONG g_workerStarted = 0;
static LONG g_configLoaded = 0;

// ---------------------------------------------------------------- 小工具
std::wstring Utf8ToWide(const std::string& s)
{
    if (s.empty()) return std::wstring();
    int len = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(len, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], len);
    return w;
}

std::wstring LastWinError(DWORD err)
{
    wchar_t* buf = nullptr;
    DWORD n = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                             nullptr, err, 0, (LPWSTR)&buf, 0, nullptr);
    std::wstring msg;
    if (n && buf)
    {
        msg.assign(buf, n);
        while (!msg.empty() && (msg.back() == L'\r' || msg.back() == L'\n' || msg.back() == L' '))
            msg.pop_back();
    }
    if (buf) LocalFree(buf);
    if (msg.empty()) msg = L"未知错误";
    wchar_t code[32];
    swprintf(code, 32, L" (Win32=%lu)", err);
    return msg + code;
}

std::wstring WeekdayCn(int wd)
{
    static const wchar_t* names[] = { L"周日", L"周一", L"周二", L"周三", L"周四", L"周五", L"周六" };
    if (wd < 0 || wd > 6) wd = 0;
    return names[wd];
}

std::wstring FormatHMS(long long sec)
{
    if (sec < 0) sec = 0;
    long long h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;
    wchar_t buf[32];
    swprintf(buf, 32, L"%lld:%02lld:%02lld", h, m, s);
    return buf;
}

std::wstring FormatLongDuration(long long sec)
{
    if (sec < 0) sec = 0;
    long long d = sec / 86400, h = (sec % 86400) / 3600, m = (sec % 3600) / 60, s = sec % 60;
    wchar_t buf[64];
    if (d > 0)
        swprintf(buf, 64, L"%lld天%lld小时%02lld分%02lld秒", d, h, m, s);
    else
        swprintf(buf, 64, L"%lld小时%02lld分%02lld秒", h, m, s);
    return buf;
}

void GetBeijingTime(SYSTEMTIME& bj)
{
    SYSTEMTIME utc;
    GetSystemTime(&utc);
    FILETIME ft;
    SystemTimeToFileTime(&utc, &ft);
    ULARGE_INTEGER u;
    u.LowPart = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart += 8ull * 3600ull * 10000000ull;   // UTC+8
    ft.dwLowDateTime = u.LowPart;
    ft.dwHighDateTime = u.HighPart;
    FileTimeToSystemTime(&ft, &bj);
}

// ---------------------------------------------------------------- 配置
std::wstring GetIniPath()
{
    return g_configDir + L"\\DeepSeekPlugin.ini";
}

static std::wstring ModuleDir()
{
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(g_hInst, path, MAX_PATH);
    std::wstring p(path);
    size_t pos = p.find_last_of(L"\\/");
    if (pos != std::wstring::npos) p.resize(pos);
    return p;
}

void LoadConfigFromIni()
{
    std::wstring ini = GetIniPath();
    wchar_t buf[1024];
    GetPrivateProfileStringW(L"config", L"CredentialFile", L"", buf, 1024, ini.c_str());
    g_config.credentialFile = buf;
    GetPrivateProfileStringW(L"config", L"ApiKey", L"", buf, 1024, ini.c_str());
    g_config.apiKey = buf;
    GetPrivateProfileStringW(L"config", L"ProxyServer", L"127.0.0.1:7897", buf, 1024, ini.c_str());
    g_config.proxyServer = buf;
    g_config.proxyMode = (int)GetPrivateProfileIntW(L"config", L"ProxyMode", 2, ini.c_str());
    if (g_config.proxyMode < 0 || g_config.proxyMode > 2) g_config.proxyMode = 2;
    g_config.refreshMinutes = (int)GetPrivateProfileIntW(L"config", L"RefreshMinutes", 10, ini.c_str());
    if (g_config.refreshMinutes < 1) g_config.refreshMinutes = 1;
    if (g_config.refreshMinutes > 1440) g_config.refreshMinutes = 1440;
}

void SaveConfigToIni()
{
    std::wstring ini = GetIniPath();
    wchar_t num[32];
    WritePrivateProfileStringW(L"config", L"CredentialFile", g_config.credentialFile.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"config", L"ApiKey", g_config.apiKey.c_str(), ini.c_str());
    WritePrivateProfileStringW(L"config", L"ProxyServer", g_config.proxyServer.c_str(), ini.c_str());
    swprintf(num, 32, L"%d", g_config.proxyMode);
    WritePrivateProfileStringW(L"config", L"ProxyMode", num, ini.c_str());
    swprintf(num, 32, L"%d", g_config.refreshMinutes);
    WritePrivateProfileStringW(L"config", L"RefreshMinutes", num, ini.c_str());
}

void EnsureConfigLoaded()
{
    if (InterlockedExchange(&g_configLoaded, 1))
        return;
    EnterCriticalSection(&g_cs);
    if (g_configDir.empty())
        g_configDir = ModuleDir();
    while (!g_configDir.empty() && g_configDir.back() == L'\\')
        g_configDir.pop_back();
    LeaveCriticalSection(&g_cs);

    CreateDirectoryW(g_configDir.c_str(), nullptr);   // 目录不存在则尝试创建(已存在时无害)

    EnterCriticalSection(&g_cs);
    bool iniExists = GetFileAttributesW(GetIniPath().c_str()) != INVALID_FILE_ATTRIBUTES;
    if (!iniExists)
        SaveConfigToIni();          // 首次运行生成默认配置, 方便手工编辑
    LoadConfigFromIni();

    // 自动迁移: 旧版配置遗留明文 Key 且默认凭据文件可用 → 改用凭据文件并清除明文
    if (g_config.credentialFile.empty() && !g_config.apiKey.empty())
    {
        std::wstring def = DefaultCredentialPath();
        std::wstring migKey, migErr;
        if (!def.empty() && ReadApiKeyFromCredentialFile(def, migKey, migErr) && !migKey.empty())
        {
            g_config.credentialFile = def;
            g_config.apiKey.clear();
            SaveConfigToIni();
        }
    }

    // 载入节假日缓存(今年与明年)
    SYSTEMTIME bj;
    GetBeijingTime(bj);
    for (int y = bj.wYear; y <= bj.wYear + 1; y++)
    {
        if (g_holidays.HasYear(y)) continue;
        std::set<int> md;
        if (LoadHolidayCache(g_configDir, y, md))
            g_holidays.SetYear(y, md, L"缓存");
    }
    LeaveCriticalSection(&g_cs);
}

void TriggerDeepSeekRefresh()
{
    CDeepSeekPlugin::Instance().StartWorker();
    g_forceRefresh = true;
    if (g_hWake) SetEvent(g_hWake);
}

// ---------------------------------------------------------------- 峰谷计算
bool IsPeakBJTime(const SYSTEMTIME& bj)
{
    // 周末(含调休上班的周末)与法定节假日全天均为空闲时段
    int wd = bj.wDayOfWeek;                 // 0=周日
    if (wd == 0 || wd == 6) return false;
    if (g_holidays.IsHoliday(bj.wYear, bj.wMonth, bj.wDay)) return false;
    int mins = bj.wHour * 60 + bj.wMinute;
    // 9:00-12:00 与 14:00-18:00 为高峰
    return (mins >= 9 * 60 && mins < 12 * 60) || (mins >= 14 * 60 && mins < 18 * 60);
}

PeriodInfo ComputePeriodFromBJTime(const SYSTEMTIME& bj)
{
    PeriodInfo info;
    EnterCriticalSection(&g_cs);
    info.isPeak = IsPeakBJTime(bj);

    // 今日描述
    bool holiday = g_holidays.IsHoliday(bj.wYear, bj.wMonth, bj.wDay);
    wchar_t dbuf[64];
    if (holiday)
        swprintf(dbuf, 64, L"%ls·法定节假日", WeekdayCn(bj.wDayOfWeek).c_str());
    else if (bj.wDayOfWeek == 0 || bj.wDayOfWeek == 6)
        swprintf(dbuf, 64, L"%ls·周末", WeekdayCn(bj.wDayOfWeek).c_str());
    else
        swprintf(dbuf, 64, L"%ls·工作日", WeekdayCn(bj.wDayOfWeek).c_str());
    info.dayDesc = dbuf;

    // 当前时刻(北京时间)的 FILETIME
    SYSTEMTIME t = bj;
    t.wSecond = 0;
    t.wMilliseconds = 0;
    FILETIME ftNow, ftCur;
    SYSTEMTIME bjTrunc = t;
    SystemTimeToFileTime(&bj, &ftNow);
    SystemTimeToFileTime(&bjTrunc, &ftCur);
    ULARGE_INTEGER uNow, uCur;
    uNow.LowPart = ftNow.dwLowDateTime;  uNow.HighPart = ftNow.dwHighDateTime;
    uCur.LowPart = ftCur.dwLowDateTime;  uCur.HighPart = ftCur.dwHighDateTime;

    // 按分钟向前扫描下一个切换点(边界总在整分钟; 节假日长假最长约 8 天, 上限 40 天)
    const ULONGLONG ONE_MIN = 60ull * 10000000ull;
    SYSTEMTIME tSwitch = t;
    for (int i = 1; i <= 60 * 24 * 40; i++)
    {
        uCur.QuadPart += ONE_MIN;
        ftCur.dwLowDateTime = uCur.LowPart;
        ftCur.dwHighDateTime = uCur.HighPart;
        FileTimeToSystemTime(&ftCur, &tSwitch);
        if (IsPeakBJTime(tSwitch) != info.isPeak)
            break;
    }
    info.switchTime = tSwitch;
    long long diff = (long long)((uCur.QuadPart - uNow.QuadPart) / 10000000ull);
    if (diff < 0) diff = 0;
    info.secondsLeft = diff;

    info.curName = info.isPeak ? L"高峰时段" : L"空闲时段";
    info.nextName = info.isPeak ? L"空闲时段" : L"高峰时段";
    info.fmtRemaining = FormatHMS(info.secondsLeft);
    {
        // 显示格式: 空闲(谷)=+ 高峰(峰)=-, HH:MM(小时可超24), 随时间向 00:00 倒计时
        long long hh = info.secondsLeft / 3600, mm = (info.secondsLeft % 3600) / 60;
        swprintf(dbuf, 64, L"%ls%02lld:%02lld", info.isPeak ? L"-" : L"+", hh, mm);
        info.fmtSignedHM = dbuf;
    }
    info.fmtLongRemaining = FormatLongDuration(info.secondsLeft);
    swprintf(dbuf, 64, L"%02d:%02d", tSwitch.wHour, tSwitch.wMinute);
    info.fmtSwitchClock = dbuf;

    // 切换日期描述
    ULARGE_INTEGER uDay1 = uNow;
    // 计算“明天”的日期
    SYSTEMTIME stTomorrow;
    ULARGE_INTEGER uTmp = uNow;
    uTmp.QuadPart += 24ull * 3600ull * 10000000ull;
    FILETIME ftTmp;
    ftTmp.dwLowDateTime = uTmp.LowPart; ftTmp.dwHighDateTime = uTmp.HighPart;
    FileTimeToSystemTime(&ftTmp, &stTomorrow);
    bool sameDay = (tSwitch.wYear == bj.wYear && tSwitch.wMonth == bj.wMonth && tSwitch.wDay == bj.wDay);
    bool nextDay = (tSwitch.wYear == stTomorrow.wYear && tSwitch.wMonth == stTomorrow.wMonth && tSwitch.wDay == stTomorrow.wDay);
    if (sameDay)
        info.switchDateText = L"今天";
    else if (nextDay)
        info.switchDateText = L"明天";
    else
    {
        swprintf(dbuf, 64, L"%02d-%02d", tSwitch.wMonth, tSwitch.wDay);
        info.switchDateText = dbuf;
    }
    info.switchDateText += L" " + WeekdayCn(tSwitch.wDayOfWeek);
    LeaveCriticalSection(&g_cs);
    return info;
}

// ---------------------------------------------------------------- 余额获取
static void SetBalanceDisplayFromState()      // 需持有 g_cs
{
    if (g_balance.ok)
    {
        if (!g_balance.isAvailable)
            g_balance.statusText = L"无可用余额";
        else
            g_balance.statusText = (g_balance.currency == L"USD" ? L"$" : L"￥") + g_balance.total;
    }
}

void CDeepSeekPlugin::FetchBalanceOnce()
{
    // 快照配置
    EnterCriticalSection(&g_cs);
    DSConfig cfg = g_config;
    LeaveCriticalSection(&g_cs);

    SYSTEMTIME lt;
    GetLocalTime(&lt);
    wchar_t timebuf[64];
    swprintf(timebuf, 64, L"%04d-%02d-%02d %02d:%02d:%02d", lt.wYear, lt.wMonth, lt.wDay, lt.wHour, lt.wMinute, lt.wSecond);

    ResolvedKey rk = ResolveApiKey(cfg);
    if (rk.key.empty())
    {
        EnterCriticalSection(&g_cs);
        g_balance.ok = false;
        g_balance.statusText = cfg.credentialFile.empty() ? L"未配置" : L"凭据错误";
        g_balance.errorText = rk.error;
        g_balance.keySource.clear();
        g_balance.lastUpdateText = timebuf;
        LeaveCriticalSection(&g_cs);
        return;
    }

    std::vector<std::wstring> headers;
    headers.push_back(L"Authorization: Bearer " + rk.key);
    headers.push_back(L"Accept: application/json");
    HttpResult r = HttpGetAuto(L"api.deepseek.com", 443, L"/user/balance", headers, cfg);

    EnterCriticalSection(&g_cs);
    g_balance.lastUpdateText = timebuf;
    g_balance.keySource = rk.source;
    if (!r.networkOk)
    {
        g_balance.ok = false;
        g_balance.statusText = L"网络错误";
        g_balance.errorText = L"请求失败: " + r.error + (r.usedProxy ? L"（已尝试代理）" : L"");
    }
    else if (r.status == 200)
    {
        std::string s;
        g_balance.ok = true;
        g_balance.errorText.clear();
        if (JsonFindBool(r.body, "is_available", g_balance.isAvailable) == false)
            g_balance.isAvailable = false;
        if (JsonFindString(r.body, "currency", s)) g_balance.currency = Utf8ToWide(s);
        if (JsonFindString(r.body, "total_balance", s)) g_balance.total = Utf8ToWide(s);
        g_balance.granted.clear();
        g_balance.toppedUp.clear();
        if (JsonFindString(r.body, "granted_balance", s)) g_balance.granted = Utf8ToWide(s);
        if (JsonFindString(r.body, "topped_up_balance", s)) g_balance.toppedUp = Utf8ToWide(s);
        SetBalanceDisplayFromState();
    }
    else if (r.status == 401)
    {
        g_balance.ok = false;
        g_balance.statusText = L"Key无效";
        g_balance.errorText = L"HTTP 401: API Key 无效或已被撤销";
    }
    else
    {
        g_balance.ok = false;
        wchar_t eb[128];
        swprintf(eb, 128, L"HTTP %d", r.status);
        g_balance.statusText = eb;
        std::string msg;
        g_balance.errorText = eb;
        if (JsonFindString(r.body, "error_message", msg))
            g_balance.errorText += L": " + Utf8ToWide(msg);
    }
    LeaveCriticalSection(&g_cs);
}

// ---------------------------------------------------------------- 后台线程
DWORD WINAPI CDeepSeekPlugin::WorkerProc(LPVOID param)
{
    ((CDeepSeekPlugin*)param)->WorkerLoop();
    return 0;
}

void CDeepSeekPlugin::WorkerLoop()
{
    EnsureConfigLoaded();

    // 先取余额(界面尽快有反馈), 再拉节假日数据(网络差时可能较慢)
    FetchBalanceOnce();
    ULONGLONG lastBalance = GetTickCount64();
    EnsureHolidaysFresh(true);
    ULONGLONG lastHoliday = GetTickCount64();

    while (!g_stop)
    {
        ULONGLONG now = GetTickCount64();
        int refreshMin;
        EnterCriticalSection(&g_cs);
        refreshMin = g_config.refreshMinutes;
        LeaveCriticalSection(&g_cs);

        bool force = g_forceRefresh.exchange(false);
        if (force || lastBalance == 0 || now - lastBalance >= (ULONGLONG)refreshMin * 60000ull)
        {
            FetchBalanceOnce();
            lastBalance = GetTickCount64();
        }
        // 每 12 小时检查一次节假日数据(缺失年份/过期缓存会自动补拉)
        if (GetTickCount64() - lastHoliday >= 12ull * 3600ull * 1000ull)
        {
            EnsureHolidaysFresh(true);
            lastHoliday = GetTickCount64();
        }
        WaitForSingleObject(g_hWake, 1000);
    }
}

void CDeepSeekPlugin::StartWorker()
{
    if (InterlockedExchange(&g_workerStarted, 1))
        return;
    if (!g_hWake)
        g_hWake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    m_hThread = CreateThread(nullptr, 0, WorkerProc, this, 0, nullptr);
}

// ---------------------------------------------------------------- ITMPlugin 实现
CDeepSeekPlugin& CDeepSeekPlugin::Instance()
{
    static CDeepSeekPlugin instance;
    return instance;
}

IPluginItem* CDeepSeekPlugin::GetItem(int index)
{
    if (index == 0) return &m_balanceItem;
    if (index == 1) return &m_periodItem;
    return nullptr;
}

void CDeepSeekPlugin::DataRequired()
{
    EnsureConfigLoaded();
    StartWorker();

    SYSTEMTIME bj;
    GetBeijingTime(bj);
    PeriodInfo pi = ComputePeriodFromBJTime(bj);

    EnterCriticalSection(&g_cs);
    // 项目显示值
    m_periodItem.SetCached(pi.fmtSignedHM);
    m_balanceItem.SetCached(g_balance.statusText);

    // 鼠标提示: 仅显示余额/赠金/充值 (获取失败时保留简短状态便于排查)
    std::wstring tip;
    if (g_balance.ok)
    {
        std::wstring sym = (g_balance.currency == L"USD" ? L"$" : L"￥");
        tip += L"余额: " + sym + g_balance.total + (g_balance.isAvailable ? L"" : L"（无可用余额!）") + L"\r\n";
        if (!g_balance.granted.empty() || !g_balance.toppedUp.empty())
            tip += L"赠金: " + sym + g_balance.granted + L" | 充值: " + sym + g_balance.toppedUp + L"\r\n";
    }
    else
    {
        tip += L"状态: " + g_balance.statusText + L"\r\n";
        if (!g_balance.errorText.empty())
            tip += g_balance.errorText + L"\r\n";
    }
    m_tooltip = tip;
    LeaveCriticalSection(&g_cs);
}

ITMPlugin::OptionReturn CDeepSeekPlugin::ShowOptionsDialog(void* hParent)
{
    return ::ShowDSOptionsDialog((HWND)hParent) ? OR_OPTION_CHANGED : OR_OPTION_UNCHANGED;
}

const wchar_t* CDeepSeekPlugin::GetInfo(PluginInfoIndex index)
{
    switch (index)
    {
    case TMI_NAME:        return L"DeepSeek 余额与峰谷时段";
    case TMI_DESCRIPTION: return L"显示 DeepSeek API 账户余额，以及距离高峰/空闲计费时段切换的倒计时。";
    case TMI_AUTHOR:      return L"DSH";
    case TMI_COPYRIGHT:   return L"Copyright (C) 2026";
    case TMI_VERSION:     return L"1.0.0";
    case TMI_URL:         return L"https://platform.deepseek.com";
    default:              return L"";
    }
}

void CDeepSeekPlugin::OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data)
{
    if (index == EI_CONFIG_DIR && data != nullptr)
    {
        EnterCriticalSection(&g_cs);
        g_configDir = data;
        while (!g_configDir.empty() && g_configDir.back() == L'\\')
            g_configDir.pop_back();
        LeaveCriticalSection(&g_cs);
        // 若此前已用兜底目录加载过配置, 则按新目录重新加载
        InterlockedExchange(&g_configLoaded, 0);
        EnsureConfigLoaded();
    }
}

const wchar_t* CDeepSeekPlugin::GetCommandName(int command_index)
{
    if (command_index == 0) return L"立即刷新 DeepSeek 余额";
    if (command_index == 1) return L"打开 DeepSeek 开放平台(用量)";
    return nullptr;
}

void CDeepSeekPlugin::OnPluginCommand(int command_index, void* hWnd, void* para)
{
    if (command_index == 0)
    {
        TriggerDeepSeekRefresh();
        if (m_pApp) m_pApp->ShowNotifyMessage(L"DeepSeek 余额：已触发刷新");
    }
    else if (command_index == 1)
    {
        ShellExecuteW(nullptr, L"open", L"https://platform.deepseek.com/usage", nullptr, nullptr, SW_SHOWNORMAL);
    }
}

int CBalanceItem::OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag)
{
    if (type == MT_DBCLICKED)
    {
        TriggerDeepSeekRefresh();
        return 1;   // 已处理, 阻止主程序默认的双击行为
    }
    return 0;
}

// ---------------------------------------------------------------- 调试导出(测试器用)
extern "C" __declspec(dllexport) const wchar_t* DSPluginDebugPeriod(const wchar_t* bjTimeIso)
{
    static wchar_t s_buf[256];
    SYSTEMTIME st = {};
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (!bjTimeIso || swscanf(bjTimeIso, L"%d-%d-%dT%d:%d:%d", &y, &mo, &d, &h, &mi, &s) < 5)
        return L"BAD_INPUT";
    st.wYear = (WORD)y; st.wMonth = (WORD)mo; st.wDay = (WORD)d;
    st.wHour = (WORD)h; st.wMinute = (WORD)mi; st.wSecond = (WORD)s;
    // 归一化(补齐 wDayOfWeek)
    FILETIME ft;
    if (!SystemTimeToFileTime(&st, &ft))
        return L"BAD_INPUT";
    FileTimeToSystemTime(&ft, &st);

    PeriodInfo pi = ComputePeriodFromBJTime(st);
    swprintf(s_buf, 256, L"%ls|%04d-%02d-%02dT%02d:%02d:%02d|%lld|%ls",
             pi.isPeak ? L"PEAK" : L"OFFPEAK",
             pi.switchTime.wYear, pi.switchTime.wMonth, pi.switchTime.wDay,
             pi.switchTime.wHour, pi.switchTime.wMinute, pi.switchTime.wSecond,
             pi.secondsLeft,
             pi.fmtSignedHM.c_str());
    return s_buf;
}

extern "C" __declspec(dllexport) const wchar_t* DSPluginDebugReadKey(const wchar_t* credPath)
{
    static wchar_t s_buf2[512];
    std::wstring key, err;
    if (ReadApiKeyFromCredentialFile(credPath ? credPath : L"", key, err))
        swprintf(s_buf2, 512, L"OK|%ls", key.c_str());
    else
        swprintf(s_buf2, 512, L"ERR|%ls", err.c_str());
    return s_buf2;
}

// ---------------------------------------------------------------- 入口
extern "C" __declspec(dllexport) ITMPlugin* TMPluginGetInstance()
{
    return &CDeepSeekPlugin::Instance();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        g_hInst = hModule;
        DisableThreadLibraryCalls(hModule);
        InitializeCriticalSection(&g_cs);
        InitBuiltinHolidays();
    }
    else if (reason == DLL_PROCESS_DETACH)
    {
        // 进程退出时其他线程已被终止, 此处只置停止标志, 不做等待/加锁清理
        g_stop = true;
    }
    return TRUE;
}
