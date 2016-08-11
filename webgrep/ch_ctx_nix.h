#ifndef CH_CTX_NIX_H
#define CH_CTX_NIX_H
#include <string>
#include <array>
#include <mutex>
#include <memory>
#include "noncopyable.hpp"

extern "C" {
#include <neon/ne_session.h>
#include <neon/ne_request.h>
#include <neon/ne_utils.h>
#include <neon/ne_uri.h>
}

namespace WebGrep {

/** libneon ne_session holder*/
class ClientCtx : public WebGrep::noncopyable
{
public:
  ClientCtx() : sess(nullptr), port(0) { }

  virtual ~ClientCtx();

  //@return TRUE if scheme is "https"
  bool isHttps() const;

  ne_session* sess;
  uint16_t port;
  std::array<char, 6> scheme;// "http\0\0" or "https\0"
  std::string response;
  std::string host_and_port;
  std::mutex mu;//locked in issueRequest()
};

struct IssuedRequest
{//ref.count holding structure
  std::shared_ptr<ne_request> req;
  std::shared_ptr<ClientCtx> ctx; //holds reference of a context
};


}//WebGrep

#endif // CH_CTX_NIX_H
