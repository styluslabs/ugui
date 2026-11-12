// libcurl backend for platformhttp.h - see platformhttp.h for usage
// - single worker thread running curl multi interface (so all requests can share connections, DNS cache, etc.)
// - define HTTP_CURL_DLOPEN to load libcurl at runtime (only curl headers are needed to build); otherwise link
//  w/ -lcurl

#include "platformhttp.h"
#include <curl/curl.h>
#include <thread>
#include <mutex>
#include <deque>
#include <list>
#include <atomic>
#include <cstring>
#include <algorithm>

#ifdef HTTP_CURL_DLOPEN
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#endif

// functions we use; curl_multi_poll and curl_multi_wakeup (curl 7.68+) are optional
#define HTTP_CURL_FNS(X) X(curl_global_init) X(curl_easy_init) X(curl_easy_setopt) X(curl_easy_getinfo) \
  X(curl_easy_cleanup) X(curl_easy_strerror) X(curl_slist_append) X(curl_slist_free_all) X(curl_multi_init) \
  X(curl_multi_add_handle) X(curl_multi_remove_handle) X(curl_multi_perform) X(curl_multi_info_read) \
  X(curl_multi_wait) X(curl_multi_setopt) X(curl_multi_cleanup) X(curl_share_init) X(curl_share_setopt) X(curl_share_cleanup)
#define HTTP_CURL_OPT_FNS(X) X(curl_multi_poll) X(curl_multi_wakeup)

namespace {

struct CurlApi
{
#define HTTP_CURL_DECL(f) decltype(&::f) f = NULL;
  HTTP_CURL_FNS(HTTP_CURL_DECL)
  HTTP_CURL_OPT_FNS(HTTP_CURL_DECL)
#undef HTTP_CURL_DECL

  bool load()
  {
#ifdef HTTP_CURL_DLOPEN
#ifdef _WIN32
    static const char* libs[] = {"libcurl.dll", "libcurl-x64.dll", "libcurl-4.dll"};
    HMODULE lib = NULL;
    for(const char* name : libs) { if((lib = LoadLibraryA(name))) break; }
    auto sym = [&](const char* name) { return lib ? GetProcAddress(lib, name) : FARPROC(NULL); };
#else
    // libcurl-gnutls.so.4 is the default on Debian/Ubuntu if libcurl4 (OpenSSL) isn't installed
    static const char* libs[] = {"libcurl.so.4", "libcurl-gnutls.so.4", "libcurl-nss.so.4", "libcurl.so",
        "libcurl.4.dylib", "/usr/lib/libcurl.4.dylib"};
    void* lib = NULL;
    for(const char* name : libs) { if((lib = dlopen(name, RTLD_NOW | RTLD_LOCAL))) break; }
    auto sym = [&](const char* name) { return lib ? dlsym(lib, name) : NULL; };
#endif
    if(!lib) return false;
    // memcpy since casting object pointer to function pointer is only conditionally supported
#define HTTP_CURL_LOAD(f) { auto p = sym(#f); static_assert(sizeof(p) == sizeof(f), ""); memcpy(&f, &p, sizeof(p)); }
    HTTP_CURL_FNS(HTTP_CURL_LOAD)
    HTTP_CURL_OPT_FNS(HTTP_CURL_LOAD)
#undef HTTP_CURL_LOAD
#define HTTP_CURL_CHECK(f) if(!f) return false;
    HTTP_CURL_FNS(HTTP_CURL_CHECK)
#undef HTTP_CURL_CHECK
#else
#define HTTP_CURL_LINK(f) f = &::f;
    HTTP_CURL_FNS(HTTP_CURL_LINK)
    HTTP_CURL_OPT_FNS(HTTP_CURL_LINK)
#undef HTTP_CURL_LINK
#endif
    return curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK;
  }
};

// loaded once; NULL if libcurl is not available
CurlApi* curlApi()
{
  static CurlApi* api = []() {
    CurlApi* a = new CurlApi;
    if(a->load()) return a;
    delete a;
    return (CurlApi*)NULL;
  }();
  return api;
}

class CurlHttpClient : public HttpClient
{
public:
  CurlHttpClient(const Options& o, CurlApi* a);
  ~CurlHttpClient() override;
  uint64_t request(HttpRequest req, Callback cb) override;
  void cancel(uint64_t id) override;
  bool ok() const { return multi != NULL; }

private:
  struct Task
  {
    uint64_t id;
    HttpRequest req;
    Callback cb;
    CURL* easy = NULL;
    curl_slist* slist = NULL;
    HttpResponse resp;
    char errbuf[CURL_ERROR_SIZE];
  };

  void run();
  void wake();
  bool start(Task* t);
  void finish(std::unique_ptr<Task> t, CURLcode res);
  void finishCancelled(std::unique_ptr<Task> t);

  static size_t onData(char* ptr, size_t size, size_t nmemb, void* userdata);
  static size_t onHeader(char* ptr, size_t size, size_t nmemb, void* userdata);

  Options opts;
  CurlApi* api;
  CURLM* multi = NULL;
  CURLSH* share = NULL;
  std::thread worker;
  std::atomic<uint64_t> nextId{1};

  // shared w/ worker thread
  std::mutex mutex;
  std::deque<std::unique_ptr<Task>> incoming;
  std::vector<uint64_t> cancelIds;
  bool stopping = false;

  // worker thread only
  std::deque<std::unique_ptr<Task>> queued;
  std::list<std::unique_ptr<Task>> active;
};

CurlHttpClient::CurlHttpClient(const Options& o, CurlApi* a) : opts(o), api(a)
{
  multi = api->curl_multi_init();
  if(!multi) return;
  // CURLPIPE_MULTIPLEX is default since curl 7.62
  api->curl_multi_setopt(multi, CURLMOPT_PIPELINING, (long)CURLPIPE_MULTIPLEX);
  // all easy handles are used on worker thread only, so no locking needed for share
  share = api->curl_share_init();
  if(share && opts.cookies)
    api->curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_COOKIE);
  if(share) {
    api->curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_DNS);
    api->curl_share_setopt(share, CURLSHOPT_SHARE, CURL_LOCK_DATA_SSL_SESSION);
  }
  worker = std::thread([this](){ run(); });
}

CurlHttpClient::~CurlHttpClient()
{
  if(worker.joinable()) {
    {
      std::lock_guard<std::mutex> lock(mutex);
      stopping = true;
    }
    wake();
    worker.join();
  }
  if(multi) api->curl_multi_cleanup(multi);
  if(share) api->curl_share_cleanup(share);
}

uint64_t CurlHttpClient::request(HttpRequest req, Callback cb)
{
  std::unique_ptr<Task> t(new Task);
  t->id = nextId++;
  t->req = std::move(req);
  t->cb = std::move(cb);
  uint64_t id = t->id;
  {
    std::lock_guard<std::mutex> lock(mutex);
    incoming.push_back(std::move(t));
  }
  wake();
  return id;
}

void CurlHttpClient::cancel(uint64_t id)
{
  {
    std::lock_guard<std::mutex> lock(mutex);
    cancelIds.push_back(id);
  }
  wake();
}

void CurlHttpClient::wake()
{
  if(api->curl_multi_wakeup)
    api->curl_multi_wakeup(multi);
}

size_t CurlHttpClient::onData(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  Task* t = static_cast<Task*>(userdata);
  t->resp.body.insert(t->resp.body.end(), ptr, ptr + size*nmemb);
  return size*nmemb;
}

// called for each header line, including status line (for each response if redirected or 1xx)
size_t CurlHttpClient::onHeader(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  Task* t = static_cast<Task*>(userdata);
  size_t n = size*nmemb;
  if(n >= 5 && strncmp(ptr, "HTTP/", 5) == 0)
    t->resp.headers.clear();  // new response
  else if(n > 2 || (n > 0 && ptr[0] != '\r' && ptr[0] != '\n'))
    t->resp.headers.append(ptr, n);
  return n;
}

bool CurlHttpClient::start(Task* t)
{
  CURL* e = t->easy = api->curl_easy_init();
  if(!e) return false;
  const HttpRequest& req = t->req;
  t->errbuf[0] = '\0';
  api->curl_easy_setopt(e, CURLOPT_URL, req.url.c_str());
  api->curl_easy_setopt(e, CURLOPT_PRIVATE, (void*)t);
  api->curl_easy_setopt(e, CURLOPT_WRITEFUNCTION, &CurlHttpClient::onData);
  api->curl_easy_setopt(e, CURLOPT_WRITEDATA, (void*)t);
  api->curl_easy_setopt(e, CURLOPT_HEADERFUNCTION, &CurlHttpClient::onHeader);
  api->curl_easy_setopt(e, CURLOPT_HEADERDATA, (void*)t);
  api->curl_easy_setopt(e, CURLOPT_ERRORBUFFER, t->errbuf);
  api->curl_easy_setopt(e, CURLOPT_NOSIGNAL, 1L);  // required for multithreaded use
  api->curl_easy_setopt(e, CURLOPT_NOPROGRESS, 1L);
  api->curl_easy_setopt(e, CURLOPT_CONNECTTIMEOUT_MS, long(opts.connectTimeout*1000));
  double timeout = req.timeout > 0 ? req.timeout : opts.timeout;
  api->curl_easy_setopt(e, CURLOPT_TIMEOUT_MS, long(timeout*1000));  // 0 = no limit
  api->curl_easy_setopt(e, CURLOPT_ACCEPT_ENCODING, "");  // all supported encodings
  if(opts.followRedirects) {
    api->curl_easy_setopt(e, CURLOPT_FOLLOWLOCATION, 1L);
    api->curl_easy_setopt(e, CURLOPT_MAXREDIRS, 10L);
  }
  api->curl_easy_setopt(e, CURLOPT_HTTP_VERSION, long(opts.http2 ? CURL_HTTP_VERSION_2TLS : CURL_HTTP_VERSION_1_1));
  if(!opts.userAgent.empty())
    api->curl_easy_setopt(e, CURLOPT_USERAGENT, opts.userAgent.c_str());
  if(share)
    api->curl_easy_setopt(e, CURLOPT_SHARE, share);
  if(opts.cookies)
    api->curl_easy_setopt(e, CURLOPT_COOKIEFILE, "");  // enable in-memory cookie engine

  std::string method = req.method.empty() ? (req.body.empty() ? "GET" : "POST") : req.method;
  if(method == "GET")
    api->curl_easy_setopt(e, CURLOPT_HTTPGET, 1L);
  else if(method == "HEAD")
    api->curl_easy_setopt(e, CURLOPT_NOBODY, 1L);
  else {
    if(method != "POST")
      api->curl_easy_setopt(e, CURLOPT_CUSTOMREQUEST, method.c_str());
    if(method == "POST" || !req.body.empty()) {
      // POSTFIELDS must be set even if empty, otherwise curl reads body from stdin
      api->curl_easy_setopt(e, CURLOPT_POSTFIELDSIZE_LARGE, (curl_off_t)req.body.size());
      api->curl_easy_setopt(e, CURLOPT_POSTFIELDS, req.body.c_str());
    }
  }
  bool hasExpect = false;
  for(auto& kv : httpParseHeaders(req.headers)) {
    std::string h = kv.first + ": " + kv.second;
    if(kv.second.empty()) h = kv.first + ";";  // curl syntax for header w/ empty value
    std::string name = kv.first;
    for(char& c : name) c = char(tolower((unsigned char)c));
    hasExpect = hasExpect || name == "expect";
    t->slist = api->curl_slist_append(t->slist, h.c_str());
  }
  // curl sends "Expect: 100-continue" for larger POST bodies, adding a round trip (or 1 sec delay if not supported)
  if(!hasExpect)
    t->slist = api->curl_slist_append(t->slist, "Expect:");
  api->curl_easy_setopt(e, CURLOPT_HTTPHEADER, t->slist);
  return api->curl_multi_add_handle(multi, e) == CURLM_OK;
}

void CurlHttpClient::finish(std::unique_ptr<Task> t, CURLcode res)
{
  if(res == CURLE_OK) {
    long code = 0;
    api->curl_easy_getinfo(t->easy, CURLINFO_RESPONSE_CODE, &code);
    t->resp.status = int(code);
  }
  if(t->resp.status <= 0) {
    t->resp.status = 0;
    t->resp.error = t->errbuf[0] ? t->errbuf : api->curl_easy_strerror(res);
    if(t->resp.error.empty()) t->resp.error = "request failed";
  }
  if(t->easy) {
    api->curl_multi_remove_handle(multi, t->easy);
    api->curl_easy_cleanup(t->easy);
  }
  if(t->slist) api->curl_slist_free_all(t->slist);
  t->easy = NULL;
  t->slist = NULL;
  if(t->cb) t->cb(std::move(t->resp));
}

void CurlHttpClient::finishCancelled(std::unique_ptr<Task> t)
{
  t->resp = HttpResponse();
  strcpy(t->errbuf, CANCELLED);
  finish(std::move(t), CURLE_ABORTED_BY_CALLBACK);
}

void CurlHttpClient::run()
{
  for(;;) {
    std::vector<uint64_t> cancels;
    bool stop;
    {
      std::lock_guard<std::mutex> lock(mutex);
      for(auto& t : incoming)
        queued.push_back(std::move(t));
      incoming.clear();
      cancels.swap(cancelIds);
      stop = stopping;
    }
    for(uint64_t id : cancels) {
      auto ait = std::find_if(active.begin(), active.end(), [id](const std::unique_ptr<Task>& t){ return t->id == id; });
      if(ait != active.end()) {
        std::unique_ptr<Task> t = std::move(*ait);
        active.erase(ait);
        finishCancelled(std::move(t));
        continue;
      }
      auto qit = std::find_if(queued.begin(), queued.end(), [id](const std::unique_ptr<Task>& t){ return t->id == id; });
      if(qit != queued.end()) {
        std::unique_ptr<Task> t = std::move(*qit);
        queued.erase(qit);
        finishCancelled(std::move(t));
      }
    }
    if(stop) {
      while(!active.empty()) {
        std::unique_ptr<Task> t = std::move(active.front());
        active.pop_front();
        finishCancelled(std::move(t));
      }
      while(!queued.empty()) {
        std::unique_ptr<Task> t = std::move(queued.front());
        queued.pop_front();
        finishCancelled(std::move(t));
      }
      // requests made by callbacks above
      std::lock_guard<std::mutex> lock(mutex);
      if(incoming.empty()) break;
      continue;
    }
    while(int(active.size()) < opts.maxActive && !queued.empty()) {
      std::unique_ptr<Task> t = std::move(queued.front());
      queued.pop_front();
      if(start(t.get()))
        active.push_back(std::move(t));
      else {
        strcpy(t->errbuf, "unable to start request");
        finish(std::move(t), CURLE_FAILED_INIT);
      }
    }
    int running = 0;
    api->curl_multi_perform(multi, &running);
    CURLMsg* msg;
    int nmsgs;
    while((msg = api->curl_multi_info_read(multi, &nmsgs))) {
      if(msg->msg != CURLMSG_DONE) continue;
      auto it = std::find_if(active.begin(), active.end(),
          [msg](const std::unique_ptr<Task>& t){ return t->easy == msg->easy_handle; });
      if(it == active.end()) continue;
      std::unique_ptr<Task> t = std::move(*it);
      active.erase(it);
      finish(std::move(t), msg->data.result);
    }
    if(!queued.empty() && int(active.size()) < opts.maxActive)
      continue;  // start more requests immediately
    // wait for activity; curl_multi_poll can be interrupted by curl_multi_wakeup
    if(api->curl_multi_poll && api->curl_multi_wakeup)
      api->curl_multi_poll(multi, NULL, 0, 1000, NULL);
    else
      api->curl_multi_wait(multi, NULL, 0, 20, NULL);
  }
}

}  // namespace

std::unique_ptr<HttpClient> HttpClient::create(const Options& opts)
{
  CurlApi* api = curlApi();
  if(!api) return NULL;
  std::unique_ptr<CurlHttpClient> client(new CurlHttpClient(opts, api));
  if(!client->ok()) return NULL;
  return std::move(client);
}
