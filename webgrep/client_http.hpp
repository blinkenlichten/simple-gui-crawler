#ifndef CLIENT_HTTP_HPP
#define	CLIENT_HTTP_HPP

#include <string>
#include <memory>
#include "boost/smart_ptr/detail/spinlock.hpp"

extern "C" {
#include <neon/ne_session.h>
#include <neon/ne_request.h>
#include <neon/ne_utils.h>
#include <neon/ne_uri.h>
}

/** Modified version from https://github.com/eidheim/Simple-Web-Server */
namespace WebGrep {

std::string ExtractHostPortHttp(const std::string& targetUrl);

struct ClientCtx
{
  ClientCtx() : sess(nullptr) {
    response.reserve(512 * 1024);//512 kb
  }
  ~ClientCtx()
  {
    if (nullptr != sess)
      ne_session_destroy(sess);
  }
  ne_session* sess;
  std::string response;
  std::string host_and_port;

  boost::detail::spinlock slock;
};

class Client
{
public:
  Client();
  virtual ~Client();

  /** @return extracted host and port string.*/
  std::string connect(const std::string& httpURL);

  struct IssuedRequest
  {
   std::shared_ptr<ne_request> req;
   std::shared_ptr<ClientCtx> ctx;
  };
  IssuedRequest issueRequest(const char* method, const char* path);

protected:
  std::shared_ptr<ClientCtx> ctx;//not null when connected
};

}//WebGrep

#endif	/* CLIENT_HTTP_HPP */
