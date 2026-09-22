// Minimal blocking HTTPS client on WinHTTP -- ships with Windows, so the app
// needs no curl/OpenSSL. Thread-safe: each call opens its own connection off
// one shared session.
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace sl {

struct HttpResponse {
  int status = 0;     // 0 when the request never got a response
  std::string body;
  std::string error;  // transport-level failure (DNS, TLS, timeout, refused)
};

using HttpHeaders = std::vector<std::pair<std::string, std::string>>;

HttpResponse httpRequest(const std::string& method, const std::string& url, const HttpHeaders& headers,
                         const std::string& body, int timeoutMs = 60000);

}  // namespace sl
