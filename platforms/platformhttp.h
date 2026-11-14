#pragma once

// Asynchronous HTTP(S) client using the platform's HTTP stack, so that TLS (certificate validation, etc.),
//  proxies, and HTTP/2 are handled by the platform
// Backends (exactly one of the platform backends, which define HttpClient::create(), should be linked):
// - http_curl.cpp: libcurl (Linux; also usable on Windows); define HTTP_CURL_DLOPEN to load libcurl at runtime
//   (so it is an optional dependency) instead of linking it
// - http_unet.cpp: plain HTTP only (no HTTPS) w/ ulib/unet.h sockets - fallback if platform backend is not
//   available (e.g. libcurl not installed) and for testing; defines createUnetHttpClient() only
// - TODO: remaining platform backends (until then, apps can fall back to http_unet.cpp, i.e. plain HTTP only):
//  - http_winhttp.cpp (Windows): WinHTTP w/ WINHTTP_FLAG_ASYNC or blocking calls on worker threads; HTTP/2 via
//   WINHTTP_OPTION_ENABLE_HTTP_PROTOCOL (Win 10 1607+), decompression via WINHTTP_OPTION_DECOMPRESSION (Win 8.1+);
//   status from WINHTTP_QUERY_STATUS_CODE, headers from WINHTTP_QUERY_RAW_HEADERS_CRLF; user/password via
//   WinHttpSetCredentials w/ WINHTTP_AUTH_SCHEME_BASIC (or just set Authorization header)
//  - http_apple.mm (iOS, macOS): NSURLSession dataTaskWithRequest; NSHTTPURLResponse statusCode and
//   allHeaderFields; Options.diskCache -> NSURLCache; Options.cookies -> HTTPShouldSetCookies/ephemeral config
//  - http_android.cpp + Java class (Android): HttpURLConnection on a Java thread pool (ExecutorService), called
//   via JNI (SDL_AndroidGetJNIEnv()); read getErrorStream() for status >= 400 so body is returned;
//   user/password: set Authorization header
//  - fetch (Emscripten): emscripten_fetch w/ EMSCRIPTEN_FETCH_LOAD_TO_MEMORY
//  - see tangram-es platforms (styluslabs/tangram-ng) for existing NSURLSession and OkHttp (Android) implementations
// Usage notes:
// - callback is called exactly once for each request, on a thread owned by the client (never from within
//  request() or cancel()); it may call request() and cancel() but must not destroy the client
// - all responses that are received (any status code) are returned w/ status and body; status is 0 only if no
//  response was received (connection failed, timeout, cancelled, etc.), in which case error is set
// - for HTTP Basic auth, set HttpRequest user and password (sent w/ request, w/o waiting for 401 challenge) rather
//  than an Authorization header (some platform stacks, e.g. NSURLSession, reserve Authorization header);
//  credentials in URL are not supported by all backends
// - unet backend requires unet_init() to have been called (as the app should already do if it uses unet)

#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>
#include <cctype>

struct HttpRequest
{
  std::string url;
  std::string method;  // empty for GET (or POST if body is not empty)
  std::string headers;  // "Name: value" lines separated by "\r\n" (or "\n")
  std::string body;
  double timeout = 0;  // seconds (for entire request); 0 to use client default
  std::string user, password;  // HTTP Basic auth (if user not empty); not sent to other hosts on redirect

  HttpRequest(std::string _url = "", std::string _headers = "", std::string _body = "")
      : url(std::move(_url)), headers(std::move(_headers)), body(std::move(_body)) {}
};

struct HttpResponse
{
  int status = 0;  // HTTP status code; 0 if no response received
  std::string error;  // set iff status == 0
  std::string headers;  // response headers (excluding status line) separated by "\r\n"
  std::vector<char> body;

  bool ok() const { return status >= 200 && status < 300; }
  // returns value of first header w/ name (case insensitive), or empty string
  std::string header(const char* name) const;
};

class HttpClient
{
public:
  struct Options {
    int maxActive = 8;  // max simultaneous requests (others are queued)
    double connectTimeout = 10;  // seconds
    double timeout = 60;  // seconds, for entire request; 0 for no limit
    std::string userAgent;
    bool followRedirects = true;  // not supported by unet backend
    bool http2 = true;
    bool cookies = false;  // in-memory cookie store for this client; not supported by unet backend
    size_t diskCache = 0;  // bytes; if supported by backend
  };
  typedef std::function<void(HttpResponse&&)> Callback;
  static constexpr const char* CANCELLED = "cancelled";  // error for cancelled requests

  virtual ~HttpClient() {}  // implementations cancel outstanding requests (callbacks are called) before returning
  // returns request id (> 0) for cancel()
  virtual uint64_t request(HttpRequest req, Callback cb) = 0;
  // callback (if not already called) is called w/ status 0 and error = CANCELLED; no effect if id is invalid
  virtual void cancel(uint64_t id) = 0;

  // create client using platform backend; returns NULL if not available (e.g. libcurl not found)
  static std::unique_ptr<HttpClient> create(const Options& opts);
};

#ifdef __EMSCRIPTEN__
// fetch backend (http_fetch.cpp): send req (fetch w/ keepalive, no response) when page is unloaded (pagehide),
//  e.g. to end a server session; returns id for httpCancelOnUnload() (call when request is no longer needed)
uint64_t httpRequestOnUnload(const HttpRequest& req);
void httpCancelOnUnload(uint64_t id);
#endif

// plain HTTP client (http_unet.cpp)
std::unique_ptr<HttpClient> createUnetHttpClient(const HttpClient::Options& opts);

// split "Name: value" lines (separated by "\r\n" or "\n"); whitespace around name and value is removed
inline std::vector<std::pair<std::string, std::string>> httpParseHeaders(const std::string& headers)
{
  std::vector<std::pair<std::string, std::string>> res;
  auto trim = [](const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? std::string() : s.substr(a, b - a + 1);
  };
  size_t pos = 0;
  while(pos < headers.size()) {
    size_t eol = headers.find('\n', pos);
    if(eol == std::string::npos) eol = headers.size();
    std::string line = headers.substr(pos, eol - pos);
    size_t colon = line.find(':');
    if(colon != std::string::npos && colon > 0)
      res.emplace_back(trim(line.substr(0, colon)), trim(line.substr(colon + 1)));
    pos = eol + 1;
  }
  return res;
}

inline std::string HttpResponse::header(const char* name) const
{
  for(auto& kv : httpParseHeaders(headers)) {
    const std::string& k = kv.first;
    size_t ii = 0;
    while(ii < k.size() && name[ii] && tolower((unsigned char)k[ii]) == tolower((unsigned char)name[ii])) ++ii;
    if(ii == k.size() && !name[ii]) return kv.second;
  }
  return "";
}
