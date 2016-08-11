#include "client_http.hpp"
#include <mutex>
#include <chrono>
#include <array>
#include <cstring>
#include <stdio.h>

namespace WebGrep {

std::string ExtractHostPortHttp(const std::string& targetUrl)
{
  std::string url(targetUrl.data());
  size_t hpos = url.find_first_of("://");
  if (std::string::npos != hpos)
    {
      url = targetUrl.substr(hpos + sizeof("://")-1);
    }
  auto slash_pos = url.find_first_of('/');
  if (std::string::npos != slash_pos)
    {
      url.resize(slash_pos);
    }
  return url;
}
//-----------------------------------------------------------------
//-----------------------------------------------------------------
const char* Client::scheme() const
{
  return (nullptr == ctx)? nullptr : ctx->scheme.data();
}

uint16_t Client::port() const
{
  return (nullptr == ctx)? 0u : ctx->port;
}

#ifndef NO_LIBNEON
Client::Client()
{
  static std::once_flag flag;
  std::call_once(flag, [](){ ne_sock_init(); });
}

int AcceptAllSSL(void*, int, const ne_ssl_certificate*)
{
  return 0;//always acceptable
}

std::string Client::connect(const std::string& httpURL)
{
  auto colpos = httpURL.find_first_of("://");
  if (colpos < 4 || colpos > 5)
    return std::string();

  ctx = std::make_shared<ClientCtx>();
  ::memcpy(ctx->scheme.data(), httpURL.data(), colpos);

  for(unsigned c = 0; c < 5; ++c)
    ctx->scheme[c] = std::tolower(ctx->scheme[c]);

  ctx->host_and_port = ExtractHostPortHttp(httpURL);
  ctx->port = ctx->isHttps() ? 443 : 80;
  ne_session* ne = nullptr;
  auto pos = ctx->host_and_port.find_first_of(':');
  if (std::string::npos != pos)
    {//case format host.com:443
      char* end = nullptr;
      ctx->port = ::strtol(ctx->host_and_port.data() + (1 + pos), &end, 10);
      std::array<char, 80> hostStr;
      hostStr.fill(0x00);
      ::memcpy(hostStr.data(), ctx->host_and_port.data(), pos);
      ne = ne_session_create(ctx->scheme.data(), hostStr.data(), ctx->port);
    }
  else
    {//case format  host.com (no port)
      ne = ne_session_create(ctx->scheme.data(), ctx->host_and_port.data(), ctx->port);
      std::array<char,8> temp; temp.fill(0);
      ::snprintf(temp.data(), temp.size(), ":%u", ctx->port);
      ctx->host_and_port.append(temp.data());
    }
  ctx->sess = ne;
  ne_set_useragent(ctx->sess, "libneon");
  if (ctx->isHttps())
    {
      ne_ssl_trust_default_ca(ne);
      ne_ssl_set_verify(ne, &AcceptAllSSL, nullptr);
    }
  return ctx->host_and_port;
}

static int httpResponseReader(void* userdata, const char* buf, size_t len)
{
  ClientCtx* ctx = (ClientCtx*)userdata;
  ctx->response.append(buf, len);
  return 0;
}

WebGrep::IssuedRequest Client::issueRequest(const char* method, const char* path, bool withLock)
{
  std::shared_ptr<std::lock_guard<std::mutex>> lk;
  if (withLock) {
      lk = std::make_shared<std::lock_guard<std::mutex>>(ctx->mu);
    }
  ctx->response.clear();
  auto rq = ne_request_create(ctx->sess, method, path);
  ne_add_response_body_reader(rq, ne_accept_always, httpResponseReader, (void*)ctx.get());
  IssuedRequest out;
  out.ctx = ctx;
  out.req = std::shared_ptr<ne_request>(rq, [out](ne_request* ptr){ne_request_destroy(ptr);} );
  return out;
}
#else //case NO_LIBNEON
//-----------------------------------------------------------------
//----------------------------------
Client::Client()
{

}

std::string Client::connect(const std::string& httpURL)
{//temporary for Windows: do not really connect, just fill the fields
  auto colpos = httpURL.find_first_of("://");
  if (colpos < 4 || colpos > 5)
    return std::string();

  ctx = std::make_shared<ClientCtx>();
  ctx->scheme.fill(0x00);
  ::memcpy(ctx->scheme.data(), httpURL.data(), colpos);

  for(unsigned c = 0; c < 5; ++c)
    ctx->scheme[c] = std::tolower(ctx->scheme[c]);

  ctx->host_and_port = ExtractHostPortHttp(httpURL);
  ctx->port = (ctx->isHttps()) ? 443 : 80;
  auto pos = ctx->host_and_port.find_first_of(':');
  if (std::string::npos != pos)
    {//case user provided format host.com:8080
      char* end = nullptr;
      ctx->port = ::strtol(ctx->host_and_port.data() + (1 + pos), &end, 10);
    }

  QString host = QString::fromStdString(ctx->host_and_port.substr(0, pos));
  if (ctx->isHttps())
    ctx->mgr->connectToHostEncrypted(host, ctx->port);
  else
    ctx->mgr->connectToHost(host, ctx->port);
  return ctx->host_and_port;
}

WebGrep::IssuedRequest Client::issueRequest(const char* method, const char* path, bool withLock)
{
  (void)method;
  std::shared_ptr<std::lock_guard<std::mutex>> lk;
  if (withLock) {
      lk = std::make_shared<std::lock_guard<std::mutex>>(ctx->mu);
    }
  ctx->response.clear();

  IssuedRequest out;
  QString url = scheme();
  url += "://";
  url += ctx->host_and_port.data();
  url += path;
  out.req.setUrl(url);
  out.req.setRawHeader("User-Agent", "Qt5GET 1.0");
  out.ctx = ctx;
  return out;
}

#endif//NO_LIBNEON


}//WebGrep
