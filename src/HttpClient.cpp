/*********************************************************
* DeepSeekPlugin - WinHTTP 客户端 + JSON 简易解析
**********************************************************/
#include "DeepSeekPlugin.h"

HttpResult HttpGet(const std::wstring& host, unsigned short port, const std::wstring& path,
                   const std::vector<std::wstring>& headers, const std::wstring& proxy)
{
    HttpResult r;
    r.usedProxy = !proxy.empty();

    DWORD accessType = proxy.empty() ? WINHTTP_ACCESS_TYPE_NO_PROXY : WINHTTP_ACCESS_TYPE_NAMED_PROXY;
    HINTERNET hSession = WinHttpOpen(L"TrafficMonitor-DeepSeekPlugin/1.0",
                                     accessType,
                                     proxy.empty() ? WINHTTP_NO_PROXY_NAME : proxy.c_str(),
                                     WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession)
    {
        r.error = L"WinHttpOpen 失败: " + LastWinError(GetLastError());
        return r;
    }
    WinHttpSetTimeouts(hSession, 8000, 8000, 10000, 15000);

    HINTERNET hConnect = WinHttpConnect(hSession, host.c_str(), port, 0);
    if (!hConnect)
    {
        r.error = L"WinHttpConnect 失败: " + LastWinError(GetLastError());
        WinHttpCloseHandle(hSession);
        return r;
    }

    HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", path.c_str(),
                                            nullptr, WINHTTP_NO_REFERER,
                                            WINHTTP_DEFAULT_ACCEPT_TYPES,
                                            WINHTTP_FLAG_SECURE);
    if (!hRequest)
    {
        r.error = L"WinHttpOpenRequest 失败: " + LastWinError(GetLastError());
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return r;
    }

    for (const auto& h : headers)
        WinHttpAddRequestHeaders(hRequest, h.c_str(), (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD);

    BOOL ok = WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                                 WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok)
        ok = WinHttpReceiveResponse(hRequest, nullptr);
    if (!ok)
    {
        r.error = L"请求 " + host + L" 失败: " + LastWinError(GetLastError());
        WinHttpCloseHandle(hRequest);
        WinHttpCloseHandle(hConnect);
        WinHttpCloseHandle(hSession);
        return r;
    }

    DWORD status = 0, size = sizeof(status);
    WinHttpQueryHeaders(hRequest, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                        WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
    r.networkOk = true;
    r.status = (int)status;

    // 读取响应体(上限 4MB)
    for (;;)
    {
        DWORD avail = 0;
        if (!WinHttpQueryDataAvailable(hRequest, &avail) || avail == 0)
            break;
        if (r.body.size() > 4u * 1024u * 1024u)
            break;
        std::string chunk(avail, 0);
        DWORD read = 0;
        if (!WinHttpReadData(hRequest, &chunk[0], avail, &read) || read == 0)
            break;
        chunk.resize(read);
        r.body += chunk;
    }

    WinHttpCloseHandle(hRequest);
    WinHttpCloseHandle(hConnect);
    WinHttpCloseHandle(hSession);
    return r;
}

HttpResult HttpGetAuto(const std::wstring& host, unsigned short port, const std::wstring& path,
                       const std::vector<std::wstring>& headers, const DSConfig& cfg)
{
    // 0=仅直连 1=仅代理 2=自动(直连优先, 网络层失败时回退代理)
    if (cfg.proxyMode == 1 && !cfg.proxyServer.empty())
        return HttpGet(host, port, path, headers, cfg.proxyServer);

    HttpResult r = HttpGet(host, port, path, headers, L"");   // 直连
    if (cfg.proxyMode == 2 && !r.networkOk && !cfg.proxyServer.empty())
    {
        HttpResult rp = HttpGet(host, port, path, headers, cfg.proxyServer);
        if (rp.networkOk)
            return rp;
        // 两路都失败, 合并错误信息
        r.error += L"；代理也失败: " + rp.error;
    }
    return r;
}

// ---------------------------------------------------------------- JSON
bool JsonFindString(const std::string& json, const std::string& key, std::string& out)
{
    size_t p = json.find("\"" + key + "\"");
    if (p == std::string::npos) return false;
    p = json.find(':', p);
    if (p == std::string::npos) return false;
    p++;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\r' || json[p] == '\n'))
        p++;
    if (p >= json.size() || json[p] != '"') return false;
    size_t q = p + 1;
    std::string val;
    while (q < json.size() && json[q] != '"')
    {
        if (json[q] == '\\' && q + 1 < json.size()) { val += json[q + 1]; q += 2; }
        else { val += json[q]; q++; }
    }
    if (q >= json.size()) return false;
    out = val;
    return true;
}

bool JsonFindBool(const std::string& json, const std::string& key, bool& out)
{
    size_t p = json.find("\"" + key + "\"");
    if (p == std::string::npos) return false;
    p = json.find(':', p);
    if (p == std::string::npos) return false;
    p++;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t' || json[p] == '\r' || json[p] == '\n'))
        p++;
    if (json.compare(p, 4, "true") == 0) { out = true; return true; }
    if (json.compare(p, 5, "false") == 0) { out = false; return true; }
    return false;
}
