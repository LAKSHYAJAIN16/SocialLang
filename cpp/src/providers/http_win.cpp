#include "providers/http_win.h"

#include <windows.h>
#include <winhttp.h>

#include <mutex>

namespace sl {

namespace {

std::wstring widen(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
  std::wstring out(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), n);
  return out;
}

std::string lastErrorText(const char* what) {
  DWORD code = GetLastError();
  switch (code) {
    case ERROR_WINHTTP_TIMEOUT: return std::string(what) + ": timed out";
    case ERROR_WINHTTP_NAME_NOT_RESOLVED: return std::string(what) + ": host not found";
    case ERROR_WINHTTP_CANNOT_CONNECT: return std::string(what) + ": connection refused (is the server running?)";
    case ERROR_WINHTTP_SECURE_FAILURE: return std::string(what) + ": TLS handshake failed";
    default: return std::string(what) + ": WinHTTP error " + std::to_string(code);
  }
}

HINTERNET session() {
  static HINTERNET s = nullptr;
  static std::once_flag once;
  std::call_once(once, [] {
    s = WinHttpOpen(L"SocialSandbox/0.1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME,
                    WINHTTP_NO_PROXY_BYPASS, 0);
  });
  return s;
}

struct Handle {
  HINTERNET h = nullptr;
  ~Handle() {
    if (h) WinHttpCloseHandle(h);
  }
};

}  // namespace

HttpResponse httpRequest(const std::string& method, const std::string& url, const HttpHeaders& headers,
                         const std::string& body, int timeoutMs) {
  HttpResponse out;
  HINTERNET ses = session();
  if (!ses) {
    out.error = lastErrorText("WinHttpOpen");
    return out;
  }

  std::wstring wurl = widen(url);
  URL_COMPONENTS uc{};
  uc.dwStructSize = sizeof uc;
  wchar_t host[256], path[2048];
  uc.lpszHostName = host;
  uc.dwHostNameLength = 256;
  uc.lpszUrlPath = path;
  uc.dwUrlPathLength = 2048;
  wchar_t extra[2048];
  uc.lpszExtraInfo = extra;
  uc.dwExtraInfoLength = 2048;
  if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
    out.error = "invalid URL: " + url;
    return out;
  }
  std::wstring fullPath = std::wstring(path, uc.dwUrlPathLength) + std::wstring(extra, uc.dwExtraInfoLength);
  bool https = uc.nScheme == INTERNET_SCHEME_HTTPS;

  Handle conn{WinHttpConnect(ses, std::wstring(host, uc.dwHostNameLength).c_str(), uc.nPort, 0)};
  if (!conn.h) {
    out.error = lastErrorText("connect");
    return out;
  }
  Handle req{WinHttpOpenRequest(conn.h, widen(method).c_str(), fullPath.c_str(), nullptr, WINHTTP_NO_REFERER,
                                WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0)};
  if (!req.h) {
    out.error = lastErrorText("open request");
    return out;
  }
  WinHttpSetTimeouts(req.h, timeoutMs, timeoutMs, timeoutMs, timeoutMs);

  std::wstring headerBlock;
  for (auto& [k, v] : headers) headerBlock += widen(k) + L": " + widen(v) + L"\r\n";
  if (!WinHttpSendRequest(req.h, headerBlock.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headerBlock.c_str(),
                          static_cast<DWORD>(-1L), body.empty() ? WINHTTP_NO_REQUEST_DATA : (LPVOID)body.data(),
                          static_cast<DWORD>(body.size()), static_cast<DWORD>(body.size()), 0) ||
      !WinHttpReceiveResponse(req.h, nullptr)) {
    out.error = lastErrorText("request");
    return out;
  }

  DWORD status = 0, size = sizeof status;
  WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                      &status, &size, WINHTTP_NO_HEADER_INDEX);
  out.status = static_cast<int>(status);

  for (;;) {
    DWORD avail = 0;
    if (!WinHttpQueryDataAvailable(req.h, &avail) || avail == 0) break;
    size_t old = out.body.size();
    out.body.resize(old + avail);
    DWORD read = 0;
    if (!WinHttpReadData(req.h, out.body.data() + old, avail, &read)) break;
    out.body.resize(old + read);
  }
  return out;
}

}  // namespace sl
