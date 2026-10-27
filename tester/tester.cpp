/*********************************************************
* DeepSeekPlugin 控制台测试器
* 1) 峰谷时段计算单元测试(通过 DSPluginDebugPeriod 导出)
* 2) 模拟 TrafficMonitor 加载插件的冒烟测试
* 用法: 与 DeepSeekPlugin.dll 同目录运行 tester.exe
*       如需测试真实余额, 先在 testcfg\DeepSeekPlugin.ini 填入 ApiKey
**********************************************************/
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include "../include/PluginInterface.h"

typedef ITMPlugin* (*PFN_GetInstance)();
typedef const wchar_t* (*PFN_DebugPeriod)(const wchar_t*);

static std::string W2U(const wchar_t* s)
{
    if (!s) return "";
    int len = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    if (len <= 1) return "";
    std::string u(len - 1, 0);
    WideCharToMultiByte(CP_UTF8, 0, s, -1, &u[0], len, nullptr, nullptr);
    return u;
}

static void printW(const wchar_t* s) { printf("%s\n", W2U(s).c_str()); }

struct Case
{
    const wchar_t* iso;          // 北京时间
    const wchar_t* expectPeak;   // PEAK / OFFPEAK
    long long expectSec;         // 距切换秒数
    const wchar_t* expectText;   // 任务栏显示文本: 谷=+ 峰=-, HH:MM
    const char* desc;
};

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    HMODULE hDll = LoadLibraryW(L"DeepSeekPlugin.dll");
    if (!hDll)
    {
        printf("LoadLibrary DeepSeekPlugin.dll failed, err=%lu\n", GetLastError());
        return 1;
    }
    auto getInstance = (PFN_GetInstance)GetProcAddress(hDll, "TMPluginGetInstance");
    auto debugPeriod = (PFN_DebugPeriod)GetProcAddress(hDll, "DSPluginDebugPeriod");
    if (!getInstance || !debugPeriod)
    {
        printf("导出函数缺失: TMPluginGetInstance=%p DSPluginDebugPeriod=%p\n",
               (void*)getInstance, (void*)debugPeriod);
        return 1;
    }
    ITMPlugin* plugin = getInstance();
    if (!plugin) { printf("TMPluginGetInstance 返回空\n"); return 1; }

    // ---------------- 1. 插件信息 ----------------
    printf("=== 插件信息 ===\n");
    const char* infoNames[] = { "名称", "描述", "作者", "版权", "版本", "主页" };
    for (int i = 0; i < ITMPlugin::TMI_MAX; i++)
        printf("%s: %s\n", infoNames[i], W2U(plugin->GetInfo((ITMPlugin::PluginInfoIndex)i)).c_str());

    // ---------------- 2. 显示项目 ----------------
    printf("\n=== 显示项目 ===\n");
    for (int i = 0; i < 8; i++)
    {
        IPluginItem* item = plugin->GetItem(i);
        if (!item)
        {
            printf("GetItem(%d) = nullptr %s\n", i, i == 2 ? "(正确: 只有2个项目)" : "(越界返回空,正确)");
            break;
        }
        printf("[%d] id=%s name=%s label=%s sample=%s\n", i,
               W2U(item->GetItemId()).c_str(), W2U(item->GetItemName()).c_str(),
               W2U(item->GetItemLableText()).c_str(), W2U(item->GetItemValueSampleText()).c_str());
    }

    // ---------------- 3. 峰谷时段单元测试 ----------------
    printf("\n=== 峰谷时段单元测试 (北京时间) ===\n");
    Case cases[] = {
        { L"2026-10-23T17:47:00", L"PEAK",     780,    L"-00:13",  "周五高峰内 → 18:00 结束" },
        { L"2026-10-23T18:30:00", L"OFFPEAK",  225000, L"+62:30",  "周五晚 → 周一 09:00" },
        { L"2026-10-24T10:00:00", L"OFFPEAK",  169200, L"+47:00",  "周六 → 周一 09:00" },
        { L"2026-10-01T10:00:00", L"OFFPEAK",  601200, L"+167:00", "国庆节假日 → 10-08(周四) 09:00" },
        { L"2026-10-26T08:59:59", L"OFFPEAK",  1,      L"+00:00",  "周一高峰前 1 秒" },
        { L"2026-10-26T06:00:00", L"OFFPEAK",  10800,  L"+03:00",  "用户例1: 工作日早上6:00 → 09:00 高峰" },
        { L"2026-10-26T10:00:00", L"PEAK",     7200,   L"-02:00",  "用户例2: 工作日10:00 → 12:00 空闲" },
        { L"2026-10-24T06:00:00", L"OFFPEAK",  183600, L"+51:00",  "周六6:00(全天空闲) → 周一 09:00" },
        { L"2026-10-26T09:00:00", L"PEAK",     10800,  L"-03:00",  "周一 09:00 → 12:00" },
        { L"2026-10-26T12:00:00", L"OFFPEAK",  7200,   L"+02:00",  "周一 12:00 整 → 14:00" },
        { L"2026-10-26T12:30:00", L"OFFPEAK",  5400,   L"+01:30",  "周一午休 → 14:00" },
        { L"2026-10-26T14:00:00", L"PEAK",     14400,  L"-04:00",  "周一 14:00 → 18:00" },
        { L"2026-10-26T18:00:00", L"OFFPEAK",  54000,  L"+15:00",  "周一 18:00 整 → 周二 09:00" },
        { L"2027-01-03T10:00:00", L"OFFPEAK",  82800,  L"+23:00",  "2027周日(无节假日数据,按周末) → 周一 09:00" },
        { L"2026-12-31T23:59:00", L"OFFPEAK",  32460,  L"+09:01",  "跨年 → 2027-01-01(周五) 09:00" },
        { L"2026-02-16T10:00:00", L"OFFPEAK",  687600, L"+191:00", "春节假期(周一) → 02-24(周二) 09:00" },
        { L"2026-05-09T10:00:00", L"OFFPEAK",  169200, L"+47:00",  "调休上班的周六仍空闲 → 周一 09:00" },
        { L"2026-09-25T15:00:00", L"OFFPEAK",  237600, L"+66:00",  "中秋(周五) → 09-28(周一) 09:00" },
        { L"2026-04-06T09:30:00", L"OFFPEAK",  84600,  L"+23:30",  "清明假期(周一) 09:30 → 04-07(周二) 09:00" },
    };
    int pass = 0, fail = 0;
    for (auto& c : cases)
    {
        const wchar_t* res = debugPeriod(c.iso);
        std::string r = W2U(res);
        // 解析 "PEAK|2026-10-23T18:00:00|780|-00:13"
        bool ok = false;
        long long sec = -1;
        std::string text;
        if (r != "BAD_INPUT")
        {
            size_t p1 = r.find('|');
            size_t p2 = r.find('|', p1 + 1);
            size_t p3 = (p2 == std::string::npos) ? std::string::npos : r.find('|', p2 + 1);
            if (p1 != std::string::npos && p2 != std::string::npos && p3 != std::string::npos)
            {
                std::string peak = r.substr(0, p1);
                sec = atoll(r.substr(p2 + 1, p3 - p2 - 1).c_str());
                text = r.substr(p3 + 1);
                ok = (peak == W2U(c.expectPeak)) && (sec == c.expectSec) && (text == W2U(c.expectText));
            }
        }
        printf("%s %s 期望[%s %llds %s] 实际: %s  (%s)\n",
               ok ? "[PASS]" : "[FAIL]", W2U(c.iso).c_str(), W2U(c.expectPeak).c_str(), c.expectSec,
               W2U(c.expectText).c_str(), r.c_str(), c.desc);
        ok ? pass++ : fail++;
    }
    printf("结果: %d 通过, %d 失败\n", pass, fail);

    // ---------------- 3.5 凭据文件解析测试 ----------------
    printf("\n=== 凭据文件解析测试 ===\n");
    typedef const wchar_t* (*PFN_DebugReadKey)(const wchar_t*);
    auto debugReadKey = (PFN_DebugReadKey)GetProcAddress(hDll, "DSPluginDebugReadKey");
    if (!debugReadKey)
    {
        printf("[FAIL] 缺少导出 DSPluginDebugReadKey\n");
        fail++;
    }
    else
    {
        CreateDirectoryW(L"testcfg", nullptr);
        const char* sampleYaml =
            "version: 1\r\n"
            "records:\r\n"
            "  dummy:\r\n"
            "    kind: grant\r\n"
            "refs:\r\n"
            "  OTHER_KEY: sk-not-this\r\n"
            "  DEEPSEEK_API_KEY: sk-test-abcdef123456   # comment\r\n"
            "  QUOTED: \"sk-quoted\"\r\n";
        const char* noKeyYaml = "version: 1\r\nrefs:\r\n  OTHER: x\r\n";
        FILE* f = fopen("testcfg\\sample_credentials.yaml", "wb");
        if (f) { fputs(sampleYaml, f); fclose(f); }
        f = fopen("testcfg\\nokey_credentials.yaml", "wb");
        if (f) { fputs(noKeyYaml, f); fclose(f); }

        struct CredCase { const wchar_t* path; const wchar_t* expect; bool prefix; const char* desc; };
        CredCase ccases[] = {
            { L"testcfg\\sample_credentials.yaml", L"OK|sk-test-abcdef123456", false, "正常解析 refs 段 DEEPSEEK_API_KEY(去注释)" },
            { L"testcfg\\nokey_credentials.yaml",  L"ERR|",                  true,  "缺少条目时报错" },
            { L"testcfg\\missing_credentials.yaml", L"ERR|",                 true,  "文件不存在时报错" },
        };
        for (auto& cc : ccases)
        {
            std::string r = W2U(debugReadKey(cc.path));
            std::string exp = W2U(cc.expect);
            bool ok = cc.prefix ? (r.compare(0, exp.size(), exp) == 0) : (r == exp);
            printf("%s %s 期望[%s%s] 实际: %s  (%s)\n",
                   ok ? "[PASS]" : "[FAIL]", W2U(cc.path).c_str(), exp.c_str(),
                   cc.prefix ? "..." : "", r.c_str(), cc.desc);
            ok ? pass++ : fail++;
        }
    }
    printf("累计: %d 通过, %d 失败\n", pass, fail);

    // ---------------- 4. 冒烟测试(模拟 TrafficMonitor) ----------------
    printf("\n=== 冒烟测试 (DataRequired 循环, 凭据文件模式) ===\n");
    CreateDirectoryW(L"testcfg", nullptr);
    // 预置配置: 凭据文件指向样例文件(插件配置中不存 Key 明文)
    WritePrivateProfileStringW(L"config", L"CredentialFile",
                               L"testcfg\\sample_credentials.yaml",
                               L"testcfg\\DeepSeekPlugin.ini");
    plugin->OnInitialize(nullptr);
    plugin->OnExtenedInfo(ITMPlugin::EI_CONFIG_DIR, L"testcfg");

    IPluginItem* balItem = plugin->GetItem(0);
    IPluginItem* perItem = plugin->GetItem(1);
    bool tipClean = false;
    for (int i = 0; i < 12; i++)
    {
        plugin->DataRequired();
        printf("t=%2ds  余额[%s]=%s  峰谷[%s]=%s\n", i,
               W2U(balItem->GetItemLableText()).c_str(), W2U(balItem->GetItemValueText()).c_str(),
               W2U(perItem->GetItemLableText()).c_str(), W2U(perItem->GetItemValueText()).c_str());
        if (i == 8)
        {
            printf("\n--- 鼠标提示(tooltip) ---\n");
            printW(plugin->GetTooltipInfo());
            printf("--------------------------\n\n");
            std::string tip = W2U(plugin->GetTooltipInfo());
            tipClean = tip.find("峰谷") == std::string::npos
                    && tip.find("Key 来源") == std::string::npos
                    && tip.find("更新于") == std::string::npos
                    && tip.find("节假日") == std::string::npos
                    && !tip.empty();
            printf("%s tooltip 仅含余额信息(无峰谷/来源/更新时间等冗余)\n", tipClean ? "[PASS]" : "[FAIL]");
            tipClean ? pass++ : fail++;
        }
        Sleep(1000);
    }

    printf("\n测试结束。配置文件 testcfg\\DeepSeekPlugin.ini 仅保存凭据文件路径, 不含 Key 明文。\n");
    printf("最终结果: %d 通过, %d 失败\n", pass, fail);
    return fail == 0 ? 0 : 2;
}
