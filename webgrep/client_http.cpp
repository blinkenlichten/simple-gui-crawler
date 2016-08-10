#include "client_http.hpp"
#include <mutex>
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

Client::Client()
{
  static std::once_flag flag;
  std::call_once(flag, [](){ ne_sock_init(); });
  scheme.fill(0x00);
}

Client::~Client()
{
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
  ::memcpy(scheme.data(), httpURL.data(), colpos);

  for(unsigned c = 0; c < 5; ++c)
    scheme[c] = std::tolower(scheme[c]);

  ctx = std::make_shared<ClientCtx>();
  ctx->host_and_port = ExtractHostPortHttp(httpURL);
  int port = (0 == ::memcmp(scheme.data(), "https", 5)) ? 443 : 80;
  ne_session* ne = nullptr;
  auto pos = ctx->host_and_port.find_first_of(':');
  if (std::string::npos != pos)
    {//case format host.com:443
      char* end = nullptr;
      port = ::strtol(ctx->host_and_port.data() + (1 + pos), &end, 10);
      std::array<char, 80> hostStr;
      hostStr.fill(0x00);
      ::memcpy(hostStr.data(), ctx->host_and_port.data(), pos);
      ne = ne_session_create(scheme.data(), hostStr.data(), port);
    }
  else
    {//case format  host.com (no port)
      ne = ne_session_create(scheme.data(), ctx->host_and_port.data(), port);
      std::array<char,8> temp; temp.fill(0);
      ::snprintf(temp.data(), temp.size(), ":%u", port);
      ctx->host_and_port.append(temp.data());
    }
  ctx->sess = ne;
  ne_set_useragent(ctx->sess, "libneon");
  if (0 == ::memcmp(scheme.data(), "https", 5))
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

Client::IssuedRequest Client::issueRequest(const char* method, const char* path, bool withLock)
{
  std::shared_ptr<std::lock_guard<std::mutex>> lk;
  if (withLock) {
      lk = std::make_shared<std::lock_guard<std::mutex>>(ctx->mu);
    }
  ctx->response.clear();
  auto rq = ne_request_create(ctx->sess, method, path);
  ne_add_response_body_reader(rq, ne_accept_always, httpResponseReader, (void*)ctx.get());
  Client::IssuedRequest out;
  out.ctx = ctx;
  out.req = std::shared_ptr<ne_request>(rq, [out](ne_request* ptr){ne_request_destroy(ptr);} );
  return out;
}

}//WebGrep
