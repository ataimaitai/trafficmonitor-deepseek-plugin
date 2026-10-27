/*********************************************************
* DeepSeekPlugin - 凭据文件读取
* 从 DSH 凭据文件(如 C:\Users\<name>\.dsh\.credentials.yaml)中
* 读取 refs: 段下的 DEEPSEEK_API_KEY 条目, 插件配置中不再保存 Key 明文。
*
* 文件格式示例:
*   version: 1
*   records:
*     ...
*   refs:
*     DEEPSEEK_API_KEY: sk-xxxxxxxxxxxxxxxx
*     OTHER_KEY: ...
**********************************************************/
#include "DeepSeekPlugin.h"

std::wstring DefaultCredentialPath()
{
    wchar_t buf[MAX_PATH] = {};
    DWORD n = GetEnvironmentVariableW(L"USERPROFILE", buf, MAX_PATH);
    if (n == 0 || n > MAX_PATH)
        return std::wstring();
    return std::wstring(buf) + L"\\.dsh\\.credentials.yaml";
}

static std::wstring TrimWs(const std::wstring& s)
{
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == L' ' || s[b] == L'\t' || s[b] == L'\r' || s[b] == L'\n')) b++;
    while (e > b && (s[e - 1] == L' ' || s[e - 1] == L'\t' || s[e - 1] == L'\r' || s[e - 1] == L'\n')) e--;
    return s.substr(b, e - b);
}

static std::wstring StripQuotes(const std::wstring& v)
{
    if (v.size() >= 2 &&
        ((v.front() == L'"' && v.back() == L'"') || (v.front() == L'\'' && v.back() == L'\'')))
        return v.substr(1, v.size() - 2);
    return v;
}

bool ReadApiKeyFromCredentialFile(const std::wstring& path, std::wstring& keyOut, std::wstring& errorOut)
{
    keyOut.clear();
    errorOut.clear();
    if (path.empty())
    {
        errorOut = L"凭据文件路径为空";
        return false;
    }

    HANDLE hFile = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                               nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE)
    {
        errorOut = L"无法打开凭据文件 " + path + L": " + LastWinError(GetLastError());
        return false;
    }
    std::string raw;
    char buf[8192];
    DWORD read = 0;
    while (ReadFile(hFile, buf, sizeof(buf), &read, nullptr) && read > 0)
    {
        if (raw.size() > 4u * 1024u * 1024u) break;
        raw.append(buf, read);
    }
    CloseHandle(hFile);

    // 编码处理: UTF-16LE BOM / UTF-8 BOM / UTF-8
    std::wstring text;
    const unsigned char* p = (const unsigned char*)raw.data();
    size_t n = raw.size();
    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE)
        text.assign(reinterpret_cast<const wchar_t*>(p + 2), (n - 2) / 2);
    else if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF)
        text = Utf8ToWide(raw.substr(3));
    else
        text = Utf8ToWide(raw);

    // 逐行查找 "DEEPSEEK_API_KEY: <value>" (不区分大小写, 与缩进/所在段无关)
    size_t pos = 0;
    while (pos <= text.size())
    {
        size_t eol = text.find(L'\n', pos);
        std::wstring line = text.substr(pos, eol == std::wstring::npos ? std::wstring::npos : eol - pos);
        pos = (eol == std::wstring::npos) ? text.size() + 1 : eol + 1;

        std::wstring t = TrimWs(line);
        if (t.empty() || t[0] == L'#')
            continue;
        size_t colon = t.find(L':');
        if (colon == std::wstring::npos)
            continue;
        std::wstring key = TrimWs(t.substr(0, colon));
        if (_wcsicmp(key.c_str(), L"DEEPSEEK_API_KEY") != 0)
            continue;
        std::wstring val = TrimWs(t.substr(colon + 1));
        size_t hash = val.find(L" #");              // 去掉行尾注释
        if (hash != std::wstring::npos)
            val = TrimWs(val.substr(0, hash));
        val = StripQuotes(val);
        if (!val.empty())
        {
            keyOut = val;
            return true;
        }
    }
    errorOut = L"凭据文件中未找到 DEEPSEEK_API_KEY 条目: " + path;
    return false;
}

ResolvedKey ResolveApiKey(const DSConfig& cfg)
{
    ResolvedKey r;
    if (!cfg.credentialFile.empty())
    {
        std::wstring err;
        if (ReadApiKeyFromCredentialFile(cfg.credentialFile, r.key, err))
        {
            r.source = cfg.credentialFile;
        }
        else
        {
            // 已配置凭据文件但读取失败: 明确报错, 不回退到明文 Key
            r.error = err;
        }
        return r;
    }
    if (!cfg.apiKey.empty())
    {
        r.key = cfg.apiKey;
        r.source = L"配置文件中的旧版明文 ApiKey（建议改用凭据文件）";
        return r;
    }
    r.error = L"未配置凭据文件：请在插件「选项」中填写凭据文件路径"
              L"（如 C:\\Users\\<用户名>\\.dsh\\.credentials.yaml，读取其中 refs 段的 DEEPSEEK_API_KEY）";
    return r;
}
