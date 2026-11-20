#define WIN32_LEAN_AND_MEAN

/*
MIT License

Copyright (c) 2026 Blitz

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#include <windows.h>
#include <tlhelp32.h>

#include <winhttp.h>
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <optional>
#include <set>
#include <thread>

#include "utils.h"

static std::set<std::wstring> processes() {
    std::set<std::wstring> names;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return names;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof(e);
    if (Process32FirstW(snap, &e)) {
        do {
            names.insert(lower(e.szExeFile));
        } while (Process32NextW(snap, &e));
    }
    CloseHandle(snap);
    return names;
}

struct Config {
    std::set<std::wstring> games;
    unsigned pollMs = 2000;
    std::wstring url = L"http://127.0.0.1:13508/mcp";
};

static Config load(const char* filename) {
    Config c;
    std::ifstream f(filename);
    if (!f) {
        std::ofstream out(filename);
        out << "poll_ms=2000\ncs2.exe\n";
        std::cout << "Created games.txt. Edit it and restart the application.\n";
        return c;
    }
    std::string line;
    while (std::getline(f, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        if (line.rfind("poll_ms=", 0) == 0) {
            try {
                c.pollMs = std::clamp(std::stoul(line.substr(8)), 500ul, 60000ul);
            } catch (...) {
                std::cerr << "Invalid poll_ms; using default.\n";
            }
        } else if (line.rfind("endpoint=", 0) == 0) {
            c.url = wide(trim(line.substr(9)));
        } else {
            c.games.insert(lower(wide(line)));
        }
    }
    return c;
}

struct HttpResult {
    DWORD status = 0;
    std::string body;
    bool ok = false;
};

static HttpResult post(const std::wstring& url, const std::string& json) {
    HttpResult result;
    URL_COMPONENTS u{};
    u.dwStructSize = sizeof(u);
    wchar_t host[256]{}, path[2048]{};
    u.lpszHostName = host;
    u.dwHostNameLength = 256;
    u.lpszUrlPath = path;
    u.dwUrlPathLength = 2048;
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &u)) return result;
    if (u.nScheme != INTERNET_SCHEME_HTTP || lower(std::wstring(host)) != L"127.0.0.1") {
        std::cerr << "Only local HTTP endpoint 127.0.0.1 is allowed.\n";
        return result;
    }
    HINTERNET session = WinHttpOpen(L"ReplayAuto/1.0", WINHTTP_ACCESS_TYPE_NO_PROXY,
                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!session) return result;
    WinHttpSetTimeouts(session, 3000, 3000, 3000, 5000);
    HINTERNET connection = WinHttpConnect(session, host, u.nPort, 0);
    if (!connection) {
        WinHttpCloseHandle(session);
        return result;
    }
    HINTERNET req = WinHttpOpenRequest(connection, L"POST", path, nullptr, WINHTTP_NO_REFERER,
                                      WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
    if (req) {
        const wchar_t* headers = L"Content-Type: application/json\r\nAccept: application/json, text/event-stream\r\nMCP-Protocol-Version: 2026-07-28\r\n";
        BOOL sent = WinHttpSendRequest(req, headers, (DWORD)-1, (void*)json.data(),
                                       (DWORD)json.size(), (DWORD)json.size(), 0);
        if (sent && WinHttpReceiveResponse(req, nullptr)) {
            DWORD len = sizeof(result.status);
            WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &result.status, &len, WINHTTP_NO_HEADER_INDEX);
            for (;;) {
                DWORD available = 0;
                if (!WinHttpQueryDataAvailable(req, &available) || available == 0) break;
                std::string chunk(available, '\0');
                DWORD read = 0;
                if (!WinHttpReadData(req, chunk.data(), available, &read)) break;
                result.body.append(chunk.data(), read);
                if (result.body.size() > 1024 * 1024) break;
            }
            result.ok = (result.status >= 200 && result.status < 300);
        }
        WinHttpCloseHandle(req);
    }
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    return result;
}

static bool responseError(const std::string& body) {
    return body.find("\"isError\":true") != std::string::npos ||
           body.find("\"isError\": true") != std::string::npos ||
           body.find("\"error\":{") != std::string::npos ||
           body.find("\"error\": {") != std::string::npos;
}

static bool call(const Config& cfg, bool enable) {
    std::string args = std::string("{\"action\":\"toggle_instant_replay\",\"enable\":") +
                       (enable ? "true" : "false") + "}";
    std::string payload = "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\","
                          "\"params\":{\"name\":\"nvapp_overlay_capture\",\"arguments\":" + args + "}}";
    auto r = post(cfg.url, payload);
    if (!r.ok || responseError(r.body)) {
        std::cerr << "MCP call failed (HTTP " << r.status << "): " << r.body.substr(0, 1000) << "\n";
        return false;
    }
    std::cout << "MCP response: " << r.body.substr(0, 1000) << "\n";
    return true;
}

static bool init(const Config& cfg) {
    auto r = post(cfg.url, "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"initialize\","
                           "\"params\":{\"protocolVersion\":\"2026-07-28\",\"capabilities\":{},"
                           "\"clientInfo\":{\"name\":\"ReplayAuto\",\"version\":\"1.0\"}}}");
    if (!r.ok || responseError(r.body)) {
        std::cerr << "MCP initialize failed (HTTP " << r.status << "): " << r.body.substr(0, 500) << "\n";
        return false;
    }
    r = post(cfg.url, "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/list\",\"params\":{}}");
    if (!r.ok || responseError(r.body) || r.body.find("nvapp_overlay_capture") == std::string::npos) {
        std::cerr << "Capture tool unavailable (HTTP " << r.status << "): " << r.body.substr(0, 500) << "\n";
        return false;
    }
    return true;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);
    Config cfg = load("games.txt");
    if (cfg.games.empty()) {
        std::cerr << "No games configured in games.txt.\n";
        return 1;
    }
    std::cout << "Monitoring " << cfg.games.size() << " game executable(s). Ctrl+C to exit.\n";
    std::optional<bool> lastRequested;
    bool ready = false;
    while (true) {
        auto running = processes();
        bool any = false;
        for (const auto& game : cfg.games) {
            if (running.contains(game)) {
                any = true;
                break;
            }
        }
        if (!ready) {
            ready = init(cfg);
            if (!ready) {
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }
        }
        if (!lastRequested || *lastRequested != any) {
            std::cout << (any ? "Game detected: request Instant Replay ON\n" :
                                "No configured games: request Instant Replay OFF\n");
            if (call(cfg, any)) {
                lastRequested = any;
            } else {
                ready = false;
                std::this_thread::sleep_for(std::chrono::seconds(5));
                continue;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(cfg.pollMs));
    }
}
