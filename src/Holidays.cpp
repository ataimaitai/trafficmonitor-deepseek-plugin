/*********************************************************
* DeepSeekPlugin - 中国法定节假日数据
* 数据来源优先级:
*   1. 本地缓存文件  <配置目录>\holidays_<年份>.txt  (每行 YYYY-MM-DD)
*   2. 网络获取 NateScarlet/holiday-cn (jsdelivr CDN → GitHub raw, 支持代理回退)
*   3. 内置 2026 年官方节假日表(依据国务院 2025-11 公布的安排)
* 只影响“高峰日”判定: 法定节假日(含连休)全天为空闲时段;
* 调休上班的周六/周日按官方规则仍为空闲时段, 因此无需记录。
**********************************************************/
#include "DeepSeekPlugin.h"

// ---------------------------------------------------------------- HolidayData
bool HolidayData::IsHoliday(int year, int mon, int day) const
{
    auto it = m_years.find(year);
    if (it == m_years.end()) return false;
    return it->second.count(mon * 100 + day) > 0;
}

bool HolidayData::HasYear(int year) const
{
    return m_years.count(year) > 0;
}

size_t HolidayData::DayCount(int year) const
{
    auto it = m_years.find(year);
    return it == m_years.end() ? 0 : it->second.size();
}

void HolidayData::SetYear(int year, const std::set<int>& mdSet, const std::wstring& source)
{
    m_years[year] = mdSet;
    m_source[year] = source;
}

std::wstring HolidayData::DescribeSource() const
{
    SYSTEMTIME bj;
    GetBeijingTime(bj);
    std::wstring s;
    wchar_t buf[128];
    for (int y = bj.wYear; y <= bj.wYear + 1; y++)
    {
        if (!s.empty()) s += L" ";
        auto it = m_years.find(y);
        if (it != m_years.end() && !it->second.empty())
        {
            auto src = m_source.find(y);
            swprintf(buf, 128, L"%d(%ls·%d天)", y, src == m_source.end() ? L"未知" : src->second.c_str(), (int)it->second.size());
            s += buf;
        }
        else
        {
            swprintf(buf, 128, L"%d(未获取,仅按周末判断)", y);
            s += buf;
        }
    }
    return s;
}

// ---------------------------------------------------------------- 内置 2026
void InitBuiltinHolidays()
{
    if (g_holidays.HasYear(2026))
        return;
    std::set<int> md = {
        // 元旦
        101, 102, 103,
        // 春节 (02-15 ~ 02-23)
        215, 216, 217, 218, 219, 220, 221, 222, 223,
        // 清明节
        404, 405, 406,
        // 劳动节 (05-01 ~ 05-05)
        501, 502, 503, 504, 505,
        // 端午节
        619, 620, 621,
        // 中秋节
        925, 926, 927,
        // 国庆节 (10-01 ~ 10-07)
        1001, 1002, 1003, 1004, 1005, 1006, 1007,
    };
    g_holidays.SetYear(2026, md, L"内置");
}

// ---------------------------------------------------------------- 解析 holiday-cn JSON
bool ParseHolidayJson(const std::string& json, int year, std::set<int>& out)
{
    out.clear();
    size_t pos = 0;
    bool found = false;
    while ((pos = json.find("\"date\":\"", pos)) != std::string::npos)
    {
        pos += 8;
        if (pos + 10 > json.size()) break;
        std::string date = json.substr(pos, 10);   // YYYY-MM-DD
        // 该条目内查找 isOffDay
        size_t iso = json.find("\"isOffDay\"", pos);
        size_t next = json.find("\"date\":\"", pos);
        if (iso != std::string::npos && (next == std::string::npos || iso < next))
        {
            size_t v = json.find(':', iso);
            if (v != std::string::npos)
            {
                bool offDay = json.compare(v + 1, 4, "true") == 0;
                if (offDay && date.size() == 10 && date[4] == '-' && date[7] == '-')
                {
                    int y = atoi(date.substr(0, 4).c_str());
                    int m = atoi(date.substr(5, 2).c_str());
                    int d = atoi(date.substr(8, 2).c_str());
                    if (y == year && m >= 1 && m <= 12 && d >= 1 && d <= 31)
                    {
                        out.insert(m * 100 + d);
                        found = true;
                    }
                }
            }
        }
        pos += 10;
    }
    return found;
}

// ---------------------------------------------------------------- 本地缓存
static std::wstring CachePath(const std::wstring& dir, int year)
{
    wchar_t buf[64];
    swprintf(buf, 64, L"\\holidays_%d.txt", year);
    return dir + buf;
}

bool LoadHolidayCache(const std::wstring& dir, int year, std::set<int>& out)
{
    out.clear();
    HANDLE hFile = CreateFileW(CachePath(dir, year).c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return false;
    std::string content;
    char buf[4096];
    DWORD read = 0;
    while (ReadFile(hFile, buf, sizeof(buf), &read, nullptr) && read > 0)
        content.append(buf, read);
    CloseHandle(hFile);

    bool found = false;
    size_t pos = 0;
    while (pos < content.size())
    {
        size_t eol = content.find('\n', pos);
        std::string line = content.substr(pos, eol == std::string::npos ? std::string::npos : eol - pos);
        pos = (eol == std::string::npos) ? content.size() : eol + 1;
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.size() != 10 || line[4] != '-' || line[7] != '-') continue;
        int y = atoi(line.substr(0, 4).c_str());
        int m = atoi(line.substr(5, 2).c_str());
        int d = atoi(line.substr(8, 2).c_str());
        if (y == year && m >= 1 && m <= 12 && d >= 1 && d <= 31)
        {
            out.insert(m * 100 + d);
            found = true;
        }
    }
    return found;
}

void SaveHolidayCache(const std::wstring& dir, int year, const std::set<int>& md)
{
    if (dir.empty()) return;
    HANDLE hFile = CreateFileW(CachePath(dir, year).c_str(), GENERIC_WRITE, 0,
                               nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
        return;
    std::string content;
    char line[32];
    for (int v : md)
    {
        sprintf(line, "%04d-%02d-%02d\r\n", year, v / 100, v % 100);
        content += line;
    }
    DWORD written = 0;
    WriteFile(hFile, content.data(), (DWORD)content.size(), &written, nullptr);
    CloseHandle(hFile);
}

// ---------------------------------------------------------------- 网络获取
bool TryFetchHolidays(int year, const DSConfig& cfg, const std::wstring& dir)
{
    wchar_t pathBuf[128];
    struct { std::wstring host; std::wstring path; std::wstring tag; } sources[2];
    swprintf(pathBuf, 128, L"/gh/NateScarlet/holiday-cn@master/%d.json", year);
    sources[0] = { L"cdn.jsdelivr.net", pathBuf, L"网络" };
    swprintf(pathBuf, 128, L"/NateScarlet/holiday-cn/master/%d.json", year);
    sources[1] = { L"raw.githubusercontent.com", pathBuf, L"网络" };

    for (auto& src : sources)
    {
        HttpResult r = HttpGetAuto(src.host, 443, src.path, {}, cfg);
        if (!r.networkOk || r.status != 200)
            continue;
        std::set<int> md;
        if (!ParseHolidayJson(r.body, year, md) || md.empty())
            continue;
        EnterCriticalSection(&g_cs);
        g_holidays.SetYear(year, md, src.tag);
        LeaveCriticalSection(&g_cs);
        SaveHolidayCache(dir, year, md);
        return true;
    }
    return false;
}

void EnsureHolidaysFresh(bool forceNetworkCheck)
{
    SYSTEMTIME bj;
    GetBeijingTime(bj);
    for (int y = bj.wYear; y <= bj.wYear + 1; y++)
    {
        EnterCriticalSection(&g_cs);
        bool have = g_holidays.HasYear(y);
        std::wstring dir = g_configDir;
        DSConfig cfg = g_config;
        LeaveCriticalSection(&g_cs);

        bool needFetch = forceNetworkCheck ? true : !have;
        if (have && needFetch)
        {
            // 缓存文件 30 天内更新过则跳过(减轻服务器压力, 兼顾假期调整/次年公布)
            std::wstring path;
            wchar_t nameBuf[64];
            swprintf(nameBuf, 64, L"\\holidays_%d.txt", y);
            path = dir + nameBuf;
            WIN32_FILE_ATTRIBUTE_DATA fad = {};
            if (GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad))
            {
                FILETIME nowFt;
                GetSystemTimeAsFileTime(&nowFt);
                ULARGE_INTEGER a, n;
                a.LowPart = fad.ftLastWriteTime.dwLowDateTime; a.HighPart = fad.ftLastWriteTime.dwHighDateTime;
                n.LowPart = nowFt.dwLowDateTime;               n.HighPart = nowFt.dwHighDateTime;
                if (n.QuadPart > a.QuadPart &&
                    n.QuadPart - a.QuadPart < 30ull * 24ull * 3600ull * 10000000ull)
                    needFetch = false;   // 缓存足够新
            }
        }
        if (!have || needFetch)
            TryFetchHolidays(y, cfg, dir);
    }
}
