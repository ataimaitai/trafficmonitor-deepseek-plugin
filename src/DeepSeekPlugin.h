/*********************************************************
* DeepSeekPlugin - TrafficMonitor 插件
* 显示 DeepSeek API 账户余额 + 高峰/空闲时段切换倒计时
*
* 峰谷规则（北京时间，来源：api-docs.deepseek.com 模型&价格页）：
*   高峰时段：周一至周五（不含中国法定节假日）9:00-12:00、14:00-18:00
*   空闲时段：其余全部时间（含周末、法定节假日全天，调休上班的周末也算空闲）
**********************************************************/
#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#include <shellapi.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include "PluginInterface.h"

// ---------------------------------------------------------------- 全局对象
extern HINSTANCE g_hInst;
extern CRITICAL_SECTION g_cs;          // 保护以下所有全局状态（可重入）

// ---------------------------------------------------------------- 配置
struct DSConfig
{
    std::wstring credentialFile;                // 凭据文件路径(读取其中 DEEPSEEK_API_KEY)
    std::wstring apiKey;                        // 旧版: 配置文件明文 Key(仅未配置凭据文件时兜底)
    std::wstring proxyServer = L"127.0.0.1:7897";
    int proxyMode = 2;                          // 0=仅直连 1=仅代理 2=自动(直连优先,失败走代理)
    int refreshMinutes = 10;                    // 余额刷新间隔(分钟)
};
extern DSConfig g_config;
extern std::wstring g_configDir;                // 不含结尾反斜杠

std::wstring GetIniPath();
void LoadConfigFromIni();
void SaveConfigToIni();
void EnsureConfigLoaded();                      // 幂等，首次确定目录并加载
void TriggerDeepSeekRefresh();                  // 触发后台立即刷新余额

// ---------------------------------------------------------------- HTTP
struct HttpResult
{
    bool networkOk = false;                     // 是否收到 HTTP 响应
    int status = 0;                             // HTTP 状态码
    std::string body;                           // 响应体(UTF-8)
    std::wstring error;                         // 网络层错误描述
    bool usedProxy = false;                     // 实际是否通过代理
};

HttpResult HttpGet(const std::wstring& host, unsigned short port, const std::wstring& path,
                   const std::vector<std::wstring>& headers, const std::wstring& proxy);
HttpResult HttpGetAuto(const std::wstring& host, unsigned short port, const std::wstring& path,
                       const std::vector<std::wstring>& headers, const DSConfig& cfg);

std::wstring Utf8ToWide(const std::string& s);
std::wstring LastWinError(DWORD err);

// ---------------------------------------------------------------- JSON 简易解析
bool JsonFindString(const std::string& json, const std::string& key, std::string& out);
bool JsonFindBool(const std::string& json, const std::string& key, bool& out);

// ---------------------------------------------------------------- 凭据文件
bool ReadApiKeyFromCredentialFile(const std::wstring& path, std::wstring& keyOut, std::wstring& errorOut);
std::wstring DefaultCredentialPath();           // %USERPROFILE%\.dsh\.credentials.yaml
struct ResolvedKey
{
    std::wstring key;       // 解析到的 Key
    std::wstring source;    // 来源描述(凭据文件路径 / 旧版配置)
    std::wstring error;     // 失败原因
};
ResolvedKey ResolveApiKey(const DSConfig& cfg);

// ---------------------------------------------------------------- 余额状态
struct BalanceState
{
    bool ok = false;                            // 最近一次获取是否成功
    bool isAvailable = false;
    std::wstring currency = L"CNY";
    std::wstring total = L"--";                 // 总余额(字符串,原样)
    std::wstring granted;                       // 赠金
    std::wstring toppedUp;                      // 充值
    std::wstring statusText = L"获取中…";       // 项目显示值(短文本)
    std::wstring errorText;                     // 详细错误(供 tooltip)
    std::wstring keySource;                     // Key 来源描述(凭据文件路径/旧版配置)
    std::wstring lastUpdateText;                // 上次更新时间文本
};
extern BalanceState g_balance;

// ---------------------------------------------------------------- 节假日数据
class HolidayData
{
public:
    bool IsHoliday(int year, int mon, int day) const;
    bool HasYear(int year) const;
    size_t DayCount(int year) const;
    void SetYear(int year, const std::set<int>& mdSet, const std::wstring& source); // md = month*100+day
    std::wstring DescribeSource() const;        // 例如 "2026(内置) 2027(网络)"
private:
    std::map<int, std::set<int>> m_years;
    std::map<int, std::wstring> m_source;
};
extern HolidayData g_holidays;                  // 由 g_cs 保护

void InitBuiltinHolidays();                     // 内置 2026 官方节假日表
bool ParseHolidayJson(const std::string& json, int year, std::set<int>& out);
bool LoadHolidayCache(const std::wstring& dir, int year, std::set<int>& out);
void SaveHolidayCache(const std::wstring& dir, int year, const std::set<int>& md);
bool TryFetchHolidays(int year, const DSConfig& cfg, const std::wstring& dir); // 成功则写入 g_holidays+缓存
void EnsureHolidaysFresh(bool forceNetworkCheck);

// ---------------------------------------------------------------- 峰谷时段计算
struct PeriodInfo
{
    bool isPeak = false;                        // 当前是否高峰
    SYSTEMTIME switchTime{};                    // 下次切换时刻(北京时间)
    long long secondsLeft = 0;                  // 距下次切换秒数
    std::wstring curName;                       // 当前时段名：高峰时段/空闲时段
    std::wstring nextName;                      // 下一时段名
    std::wstring dayDesc;                       // 今日描述：周五·工作日 等
    std::wstring fmtRemaining;                  // h:mm:ss
    std::wstring fmtSignedHM;                   // 任务栏显示值: 谷=+ 峰=-, 如 +61:47 / -04:24
    std::wstring fmtLongRemaining;              // x小时yy分zz秒
    std::wstring fmtSwitchClock;                // HH:MM
    std::wstring switchDateText;                // 今天 / 明天 / MM-DD 周X
};

void GetBeijingTime(SYSTEMTIME& bj);            // UTC+8，与系统时区无关
bool IsPeakBJTime(const SYSTEMTIME& bj);        // 需持有 g_cs（读节假日表）
PeriodInfo ComputePeriodFromBJTime(const SYSTEMTIME& bj);

std::wstring FormatHMS(long long sec);          // h:mm:ss
std::wstring FormatLongDuration(long long sec); // x小时yy分zz秒
std::wstring WeekdayCn(int wd);                 // 0=周日 … 6=周六

// ---------------------------------------------------------------- 选项对话框(纯 Win32, 无资源文件)
bool ShowDSOptionsDialog(HWND hParent);         // true=已更改

// ---------------------------------------------------------------- 显示项目
class CBalanceItem : public IPluginItem
{
public:
    const wchar_t* GetItemName() const override { return L"DeepSeek 余额"; }
    const wchar_t* GetItemId() const override { return L"DeepSeekBalance"; }
    const wchar_t* GetItemLableText() const override { return L"DS余额: "; }
    const wchar_t* GetItemValueText() const override { return m_value.c_str(); }
    const wchar_t* GetItemValueSampleText() const override { return L"￥888.88"; }
    int OnMouseEvent(MouseEventType type, int x, int y, void* hWnd, int flag) override;
    void SetCached(const std::wstring& v) { m_value = v; }
private:
    mutable std::wstring m_value = L"获取中…";
};

class CPeriodItem : public IPluginItem
{
public:
    const wchar_t* GetItemName() const override { return L"DeepSeek 峰谷倒计时"; }
    const wchar_t* GetItemId() const override { return L"DeepSeekPeriod"; }
    const wchar_t* GetItemLableText() const override { return L"DS时段: "; }
    const wchar_t* GetItemValueText() const override { return m_value.c_str(); }
    const wchar_t* GetItemValueSampleText() const override { return L"+000:00"; }
    void SetCached(const std::wstring& v) { m_value = v; }
private:
    mutable std::wstring m_value = L"+00:00";
};

// ---------------------------------------------------------------- 插件主体
class CDeepSeekPlugin : public ITMPlugin
{
public:
    static CDeepSeekPlugin& Instance();

    // ITMPlugin
    IPluginItem* GetItem(int index) override;
    void DataRequired() override;
    OptionReturn ShowOptionsDialog(void* hParent) override;
    const wchar_t* GetInfo(PluginInfoIndex index) override;
    const wchar_t* GetTooltipInfo() override { return m_tooltip.c_str(); }
    void OnExtenedInfo(ExtendedInfoIndex index, const wchar_t* data) override;
    void OnInitialize(ITrafficMonitor* pApp) override { m_pApp = pApp; }
    int GetCommandCount() override { return 2; }
    const wchar_t* GetCommandName(int command_index) override;
    void OnPluginCommand(int command_index, void* hWnd, void* para) override;

    void StartWorker();
    void FetchBalanceOnce();

private:
    CDeepSeekPlugin() = default;
    static DWORD WINAPI WorkerProc(LPVOID param);
    void WorkerLoop();

    CBalanceItem m_balanceItem;
    CPeriodItem m_periodItem;
    ITrafficMonitor* m_pApp = nullptr;
    HANDLE m_hThread = nullptr;
    std::wstring m_tooltip;
};

// 调试导出：输入北京时间 "YYYY-MM-DDTHH:MM:SS"，
// 返回 "PEAK|OFFPEAK|切换时刻|剩余秒数|项目显示文本"（供测试器验证）
extern "C" __declspec(dllexport) const wchar_t* DSPluginDebugPeriod(const wchar_t* bjTimeIso);

// 调试导出：读取凭据文件中的 DEEPSEEK_API_KEY，返回 "OK|<key>" 或 "ERR|<原因>"（供测试器验证）
extern "C" __declspec(dllexport) const wchar_t* DSPluginDebugReadKey(const wchar_t* credPath);
