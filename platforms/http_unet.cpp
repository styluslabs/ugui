// plain HTTP (no HTTPS) backend for platformhttp.h using ulib/unet.h sockets - see platformhttp.h
// - fallback if platform backend isn't available, and for testing
// - one blocking request per worker thread (up to maxActive threads); new connection for each request
//  (Connection: close); redirects and cookies are not supported; credentials in URL are not supported

#include "platformhttp.h"
#include "ulib/unet.h"
#include "ulib/stringutil.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <map>
#include <atomic>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <algorithm>

namespace {

typedef std::chrono::steady_clock Clock;

class UnetHttpClient : public HttpClient
{
public:
  UnetHttpClient(const Options& o) : opts(o) {}
  ~UnetHttpClient() override;
  uint64_t request(HttpRequest req, Callback cb) override;
  void cancel(uint64_t id) override;

private:
  struct Task
  {
    uint64_t id;
    HttpRequest req;
    Callback cb;
    std::atomic_bool cancelled{false};
  };

  void workerFn();
  HttpResponse perform(Task* t);

  Options opts;
  std::atomic<uint64_t> nextId{1};
  std::mutex mutex;
  std::condition_variable cond;
  std::deque<std::shared_ptr<Task>> queue;
  std::map<uint64_t, std::shared_ptr<Task>> tasks;  // queued and running
  std::vector<std::thread> threads;
  int idleThreads = 0;
  bool stopping = false;
};

UnetHttpClient::~UnetHttpClient()
{
  {
    std::lock_guard<std::mutex> lock(mutex);
    stopping = true;
    for(auto& kv : tasks)
      kv.second->cancelled = true;
  }
  cond.notify_all();
  for(std::thread& t : threads)
    t.join();
}

uint64_t UnetHttpClient::request(HttpRequest req, Callback cb)
{
  std::shared_ptr<Task> t = std::make_shared<Task>();
  t->id = nextId++;
  t->req = std::move(req);
  t->cb = std::move(cb);
  std::lock_guard<std::mutex> lock(mutex);
  if(stopping) t->cancelled = true;  // request() from callback during shutdown
  queue.push_back(t);
  tasks[t->id] = t;
  if(idleThreads == 0 && int(threads.size()) < std::max(opts.maxActive, 1))
    threads.emplace_back([this](){ workerFn(); });
  else
    cond.notify_one();
  return t->id;
}

void UnetHttpClient::cancel(uint64_t id)
{
  // worker thread notices flag and calls callback
  std::lock_guard<std::mutex> lock(mutex);
  auto it = tasks.find(id);
  if(it != tasks.end())
    it->second->cancelled = true;
}

void UnetHttpClient::workerFn()
{
  std::unique_lock<std::mutex> lock(mutex);
  for(;;) {
    ++idleThreads;
    cond.wait(lock, [this](){ return stopping || !queue.empty(); });
    --idleThreads;
    if(queue.empty()) break;  // stopping
    std::shared_ptr<Task> t = queue.front();
    queue.pop_front();
    lock.unlock();
    HttpResponse resp;
    if(t->cancelled)
      resp.error = CANCELLED;
    else
      resp = perform(t.get());
    if(t->cb) t->cb(std::move(resp));
    lock.lock();
    tasks.erase(t->id);
  }
}

// parse "http://host[:port][/path]"; returns false if invalid or unsupported
static bool parseUrl(const std::string& url, std::string& host, std::string& port, std::string& path, std::string* err)
{
  size_t sep = url.find("://");
  std::string scheme = sep == std::string::npos ? "" : url.substr(0, sep);
  for(char& c : scheme) c = char(tolower((unsigned char)c));
  if(scheme != "http") {
    *err = scheme == "https" ? "HTTPS not supported (no TLS support available)" : "unsupported URL: " + url;
    return false;
  }
  size_t hoststart = sep + 3;
  size_t pathstart = url.find_first_of("/?#", hoststart);
  std::string hostport = url.substr(hoststart, pathstart == std::string::npos ? std::string::npos : pathstart - hoststart);
  path = pathstart == std::string::npos ? "/" : url.substr(pathstart);
  if(path[0] != '/') path = "/" + path;
  size_t frag = path.find('#');
  if(frag != std::string::npos) path.resize(frag);
  if(hostport.find('@') != std::string::npos) {
    *err = "credentials in URL not supported";
    return false;
  }
  port = "80";
  size_t colon = hostport.rfind(':');
  size_t bracket = hostport.rfind(']');
  if(colon != std::string::npos && (bracket == std::string::npos || colon > bracket)) {
    port = hostport.substr(colon + 1);
    hostport.resize(colon);
  }
  if(hostport.size() > 1 && hostport.front() == '[' && hostport.back() == ']')
    hostport = hostport.substr(1, hostport.size() - 2);
  host = hostport;
  if(host.empty() || port.empty() || port.find_first_not_of("0123456789") != std::string::npos) {
    *err = "invalid URL: " + url;
    return false;
  }
  return true;
}

// decode chunked transfer encoding; returns false if incomplete or invalid
static bool dechunk(const std::string& in, std::vector<char>& out)
{
  size_t pos = 0;
  for(;;) {
    size_t eol = in.find("\r\n", pos);
    if(eol == std::string::npos) return false;
    char* end = NULL;
    unsigned long long len = strtoull(in.c_str() + pos, &end, 16);
    if(end == in.c_str() + pos) return false;
    pos = eol + 2;
    if(len == 0) return true;  // ignore trailers
    if(in.size() < pos + len + 2) return false;
    out.insert(out.end(), in.begin() + pos, in.begin() + pos + len);
    pos += len + 2;
  }
}

HttpResponse UnetHttpClient::perform(Task* t)
{
  HttpResponse resp;
  const HttpRequest& req = t->req;
  std::string host, port, path;
  if(!parseUrl(req.url, host, port, path, &resp.error))
    return resp;
  double timeout = req.timeout > 0 ? req.timeout : opts.timeout;
  Clock::time_point t0 = Clock::now();
  Clock::time_point deadline = timeout > 0 ? t0 + std::chrono::milliseconds(int64_t(timeout*1000)) : Clock::time_point::max();
  Clock::time_point connDeadline = std::min(deadline, t0 + std::chrono::milliseconds(int64_t(opts.connectTimeout*1000)));
  // wait for socket to be ready (UNET_RDY_RD or UNET_RDY_WR) in short steps so cancel is noticed; returns false
  //  on timeout, cancel, or error
  auto waitFor = [&](int sock, int what, Clock::time_point until) {
    for(;;) {
      if(t->cancelled) { resp.error = CANCELLED; return false; }
      double remain = std::chrono::duration<double>(until - Clock::now()).count();
      if(remain <= 0) { resp.error = "timed out"; return false; }
      int res = unet_select(what == UNET_RDY_RD ? sock : -1, what == UNET_RDY_WR ? sock : -1, std::min(remain, 0.1));
      if(res < 0) { resp.error = "socket error"; return false; }
      if(res & what) return true;
    }
  };

  // note that name lookup (in unet_socket) is blocking and can't be cancelled
  int sock = unet_socket(UNET_TCP, UNET_CONNECT, UNET_NOBLOCK, host.c_str(), port.c_str());
  if(sock < 0) {
    resp.error = "unable to connect to " + host;
    return resp;
  }
  std::string method = req.method.empty() ? (req.body.empty() ? "GET" : "POST") : req.method;
  bool ipv6 = host.find(':') != std::string::npos;
  std::string hosthdr = (ipv6 ? "[" + host + "]" : host) + (port == "80" ? "" : ":" + port);
  std::string msg = method + " " + path + " HTTP/1.1\r\nHost: " + hosthdr + "\r\nConnection: close\r\n";
  if(!opts.userAgent.empty())
    msg += "User-Agent: " + opts.userAgent + "\r\n";
  if(!req.user.empty())
    msg += "Authorization: Basic " + base64_encode(req.user + ":" + req.password) + "\r\n";
  if(!req.body.empty() || method == "POST" || method == "PUT")
    msg += "Content-Length: " + std::to_string(req.body.size()) + "\r\n";
  for(auto& kv : httpParseHeaders(req.headers))
    msg += kv.first + ": " + kv.second + "\r\n";
  msg += "\r\n";
  msg += req.body;

  std::string in;
  size_t sent = 0;
  if(!waitFor(sock, UNET_RDY_WR, connDeadline)) {
    if(resp.error == "timed out") resp.error = "connection timed out";
    unet_close(sock);
    return resp;
  }
  bool ok = true;
  std::string sendErr;  // server may have sent response (e.g. error) and closed connection
  while(sent < msg.size()) {
    int n = unet_send(sock, msg.data() + sent, int(std::min(msg.size() - sent, size_t(1) << 20)));
    if(n < 0 && unet_would_block(unet_errno())) {
      if(!waitFor(sock, UNET_RDY_WR, deadline)) { ok = false; break; }
      continue;
    }
    if(n <= 0) {
      sendErr = sent == 0 ? "unable to connect to " + host : "connection closed while sending";
      break;
    }
    sent += n;
  }
  // read until server closes connection or we have complete response
  size_t hdrend = std::string::npos;
  long long contentLength = -1;
  bool chunked = false, headOnly = method == "HEAD";
  char buf[65536];
  while(ok) {
    if(hdrend == std::string::npos && (hdrend = in.find("\r\n\r\n")) != std::string::npos) {
      // skip interim 1xx responses
      if(in.compare(0, 5, "HTTP/") == 0 && in.size() > 9 && in[9] == '1') {
        in.erase(0, hdrend + 4);
        hdrend = std::string::npos;
        continue;
      }
      std::string hdr = in.substr(0, hdrend);
      size_t eol = hdr.find("\r\n");
      resp.headers = eol == std::string::npos ? "" : hdr.substr(eol + 2) + "\r\n";
      std::string te = resp.header("Transfer-Encoding");
      for(char& c : te) c = char(tolower((unsigned char)c));
      chunked = te.find("chunked") != std::string::npos;
      std::string cl = resp.header("Content-Length");
      if(!cl.empty()) contentLength = atoll(cl.c_str());
    }
    if(hdrend != std::string::npos) {
      size_t bodylen = in.size() - hdrend - 4;
      int status = atoi(in.c_str() + 9);
      if(headOnly || status == 204 || status == 304) break;
      if(!chunked && contentLength >= 0 && bodylen >= size_t(contentLength)) break;
      if(chunked && bodylen >= 5 && in.compare(in.size() - 5, 5, "0\r\n\r\n") == 0) {
        std::vector<char> tmp;
        if(dechunk(in.substr(hdrend + 4), tmp)) break;
      }
    }
    int n = unet_recv(sock, buf, sizeof(buf));
    if(n < 0 && unet_would_block(unet_errno())) {
      if(!waitFor(sock, UNET_RDY_RD, deadline)) { ok = false; break; }
      continue;
    }
    if(n <= 0) break;  // closed
    in.append(buf, n);
  }
  unet_close(sock);
  if(!ok) {
    resp.headers.clear();
    return resp;
  }
  if(hdrend == std::string::npos || in.compare(0, 5, "HTTP/") != 0 || in.size() < 12) {
    resp.error = !sendErr.empty() ? sendErr : in.empty() ? "connection closed w/o response" : "invalid response";
    resp.headers.clear();
    return resp;
  }
  int status = atoi(in.c_str() + 9);
  std::string body = in.substr(hdrend + 4);
  if(chunked && !headOnly) {
    if(!dechunk(body, resp.body)) {
      resp.error = "incomplete response";
      resp.headers.clear();
      resp.body.clear();
      return resp;
    }
  }
  else if(!headOnly) {
    if(contentLength >= 0 && body.size() < size_t(contentLength)) {
      resp.error = "incomplete response";
      resp.headers.clear();
      return resp;
    }
    if(contentLength >= 0) body.resize(size_t(contentLength));
    resp.body.assign(body.begin(), body.end());
  }
  resp.status = status > 0 ? status : 0;
  if(!resp.status) resp.error = "invalid response";
  return resp;
}

}  // namespace

std::unique_ptr<HttpClient> createUnetHttpClient(const HttpClient::Options& opts)
{
  return std::unique_ptr<HttpClient>(new UnetHttpClient(opts));
}
