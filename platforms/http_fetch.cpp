// Emscripten (browser) backend for platformhttp.h using JS fetch() - see platformhttp.h for usage
// - callbacks are called from JS promise handlers (i.e. from the browser event loop, never from within
//  request() or cancel()), on the main (only) thread; callbacks must not suspend (JSPI)
// - browser restrictions: cross-origin requests require CORS headers from server; some headers can't be set
//  (e.g. Cookie - with Options.cookies, browser's own cookie store is used w/ credentials: 'include');
//  https page can't make http requests (mixed content); Options.userAgent, maxActive, connectTimeout, http2, diskCache
//  are ignored (browser manages connections and caching)

#include "platformhttp.h"
#include <emscripten.h>
#include <unordered_map>
#include <cstdlib>
#include <cstring>

class FetchHttpClient;

namespace {
struct PendingReq { FetchHttpClient* client; HttpClient::Callback cb; };
std::unordered_map<uint64_t, PendingReq> pendingReqs;
uint64_t nextReqId = 1;
}

EM_JS(void, jsFetchStart, (double id, const char* url, const char* method, const char* headers,
    const void* body, int bodylen, double timeout, const char* user, const char* password, int flags), {
  if(!Module.httpFetchReqs) { Module.httpFetchReqs = new Map(); }
  const ctrl = new AbortController();
  const req = { ctrl: ctrl, timer: null, reason: "" };
  Module.httpFetchReqs.set(id, req);
  const hdrs = new Headers();
  for(const line of UTF8ToString(headers).split("\n")) {
    const colon = line.indexOf(":");
    if(colon <= 0) { continue; }
    try { hdrs.append(line.substring(0, colon).trim(), line.substring(colon + 1).trim()); } catch(e) {}
  }
  const u = UTF8ToString(user);
  if(u) {
    // btoa requires Latin-1, so encode UTF-8 bytes
    const cred = new TextEncoder().encode(u + ":" + UTF8ToString(password));
    hdrs.set("Authorization", "Basic " + btoa(String.fromCharCode.apply(null, cred)));
  }
  const opts = { method: UTF8ToString(method), headers: hdrs, signal: ctrl.signal,
      credentials: (flags & 1) ? "include" : "same-origin", redirect: (flags & 2) ? "follow" : "manual",
      cache: "no-store" };
  if(bodylen > 0) { opts.body = HEAPU8.slice(body, body + bodylen); }
  if(timeout > 0) {
    req.timer = setTimeout(() => { req.reason = "timeout"; ctrl.abort(); }, timeout*1000);
  }

  const done = (status, hdrstr, bytes, error) => {
    if(req.timer) { clearTimeout(req.timer); }
    if(!Module.httpFetchReqs.delete(id)) { return; }  // client destroyed
    const hdrptr = stringToNewUTF8(hdrstr);
    const errptr = stringToNewUTF8(error);
    const n = bytes ? bytes.length : 0;
    const bodyptr = _malloc(n || 1);
    if(n) { HEAPU8.set(bytes, bodyptr); }
    Module._httpFetchDone(id, status, hdrptr, bodyptr, n, errptr);
    _free(hdrptr);  _free(errptr);  _free(bodyptr);
  };
  fetch(UTF8ToString(url), opts).then(resp => {
    let hdrstr = "";
    resp.headers.forEach((v, k) => { hdrstr += k + ": " + v + "\r\n"; });
    return resp.arrayBuffer().then(buf => done(resp.status, hdrstr, new Uint8Array(buf), ""));
  }).catch(e => {
    const err = req.reason || (e && e.name == "AbortError" ? "cancelled" : String(e));
    done(0, "", null, err || "fetch failed");
  });
});

// request sent on page unload; opts are built the same way as for jsFetchStart
EM_JS(void, jsFetchOnUnload, (double id, const char* url, const char* method, const char* headers,
    const void* body, int bodylen, const char* user, const char* password), {
  if(!Module.httpUnloadReqs) {
    Module.httpUnloadReqs = new Map();
    window.addEventListener('pagehide', () => {
      for(const [k, r] of Module.httpUnloadReqs) {
        try { fetch(r.url, r.opts).catch(e => {}); } catch(e) {}
      }
      // page may be restored from bfcache, in which case app will find session gone and reconnect
      Module.httpUnloadReqs.clear();
    });
  }
  const hdrs = new Headers();
  for(const line of UTF8ToString(headers).split("\n")) {
    const colon = line.indexOf(":");
    if(colon <= 0) { continue; }
    try { hdrs.append(line.substring(0, colon).trim(), line.substring(colon + 1).trim()); } catch(e) {}
  }
  const u = UTF8ToString(user);
  if(u) {
    const cred = new TextEncoder().encode(u + ":" + UTF8ToString(password));
    hdrs.set("Authorization", "Basic " + btoa(String.fromCharCode.apply(null, cred)));
  }
  const opts = { method: UTF8ToString(method), headers: hdrs, keepalive: true, cache: "no-store" };
  if(bodylen > 0) { opts.body = HEAPU8.slice(body, body + bodylen); }  // keepalive body limit is 64 KB
  Module.httpUnloadReqs.set(id, { url: UTF8ToString(url), opts: opts });
});

EM_JS(void, jsCancelOnUnload, (double id), {
  if(Module.httpUnloadReqs) { Module.httpUnloadReqs.delete(id); }
});

// abort; if notify is false, request is forgotten (no callback)
EM_JS(void, jsFetchCancel, (double id, int notify), {
  const req = Module.httpFetchReqs && Module.httpFetchReqs.get(id);
  if(!req) { return; }
  req.reason = "cancelled";
  if(!notify) {
    if(req.timer) { clearTimeout(req.timer); }
    Module.httpFetchReqs.delete(id);
  }
  req.ctrl.abort();
});

class FetchHttpClient : public HttpClient
{
public:
  Options opts;
  FetchHttpClient(const Options& _opts) : opts(_opts) {}
  ~FetchHttpClient() override;
  uint64_t request(HttpRequest req, Callback cb) override;
  void cancel(uint64_t id) override;
};

uint64_t FetchHttpClient::request(HttpRequest req, Callback cb)
{
  uint64_t id = nextReqId++;
  pendingReqs[id] = PendingReq{this, std::move(cb)};
  std::string method = !req.method.empty() ? req.method : (req.body.empty() ? "GET" : "POST");
  // Options.userAgent is not sent: browser sends its own, and setting it would require CORS preflight w/ server
  //  allowing User-Agent header
  const std::string& headers = req.headers;
  int flags = (opts.cookies ? 1 : 0) | (opts.followRedirects ? 2 : 0);
  jsFetchStart(double(id), req.url.c_str(), method.c_str(), headers.c_str(), req.body.data(),
      int(req.body.size()), req.timeout > 0 ? req.timeout : opts.timeout, req.user.c_str(),
      req.password.c_str(), flags);
  return id;
}

// callback is called asynchronously when fetch promise rejects w/ AbortError
void FetchHttpClient::cancel(uint64_t id)
{
  auto it = pendingReqs.find(id);
  if(it != pendingReqs.end() && it->second.client == this)
    jsFetchCancel(double(id), 1);
}

FetchHttpClient::~FetchHttpClient()
{
  for(auto it = pendingReqs.begin(); it != pendingReqs.end();) {
    if(it->second.client != this) { ++it; continue; }
    jsFetchCancel(double(it->first), 0);
    Callback cb = std::move(it->second.cb);
    it = pendingReqs.erase(it);
    HttpResponse resp;
    resp.error = CANCELLED;
    if(cb) cb(std::move(resp));
  }
}

extern "C" EMSCRIPTEN_KEEPALIVE
void httpFetchDone(double _id, int status, const char* headers, const char* body, int bodylen, const char* error)
{
  auto it = pendingReqs.find(uint64_t(_id));
  if(it == pendingReqs.end()) return;
  HttpClient::Callback cb = std::move(it->second.cb);
  pendingReqs.erase(it);
  HttpResponse resp;
  resp.status = status;
  resp.headers = headers;
  resp.body.assign(body, body + bodylen);
  if(status == 0)
    resp.error = error[0] ? error : "request failed";
  if(cb) cb(std::move(resp));
}

uint64_t httpRequestOnUnload(const HttpRequest& req)
{
  uint64_t id = nextReqId++;
  std::string method = !req.method.empty() ? req.method : (req.body.empty() ? "GET" : "POST");
  jsFetchOnUnload(double(id), req.url.c_str(), method.c_str(), req.headers.c_str(), req.body.data(),
      int(req.body.size()), req.user.c_str(), req.password.c_str());
  return id;
}

void httpCancelOnUnload(uint64_t id)
{
  if(id) jsCancelOnUnload(double(id));
}

std::unique_ptr<HttpClient> HttpClient::create(const Options& opts)
{
  return std::unique_ptr<HttpClient>(new FetchHttpClient(opts));
}
