#ifndef CLIENT_HTTP_HPP
#define	CLIENT_HTTP_HPP

#include <string>
#include <memory>
#include <mutex>
#include "boost/noncopyable.hpp"

extern "C" {
#include <neon/ne_session.h>
#include <neon/ne_request.h>
#include <neon/ne_utils.h>
#include <neon/ne_uri.h>
}

/** Modified version from https://github.com/eidheim/Simple-Web-Server */
namespace WebGrep {

class ClientCtx;
std::string ExtractHostPortHttp(const std::string& targetUrl);

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
/** Contains connection context and it's dependant request tasks,
 * the destructor will clean up it all.
*/
class Client
{
public:
  struct IssuedRequest
  {//ref.count holding structure
   std::shared_ptr<ne_request> req;
   std::shared_ptr<ClientCtx> ctx;
  };

  Client();
  virtual ~Client();

  /** Connect to a host, use issueRequest() when connected.
   * Thread-safe: shared_ptr<ClientCtx> is constructed on each connect.
   * @return extracted host and port string or empty on fail.*/
  std::string connect(const std::string& httpURL);

  //can be used concurrentthread-safe locking(default is not)
  IssuedRequest issueRequest(const char* method, const char* path,
                             bool withLock = false);

protected:
  std::shared_ptr<ClientCtx> ctx;//not null when connected
};
//-----------------------------------------------------------------------------
/** libneon ne_session holder*/
class ClientCtx : public boost::noncopyable
{
public:
  ClientCtx() : sess(nullptr) {

  }
  ~ClientCtx()
  {
    if (nullptr != sess)
      ne_session_destroy(sess);
  }
  ne_session* sess;
  std::string response;
  std::string host_and_port;
  std::mutex mu;//locked in issueRequest()
};
//-----------------------------------------------------------------------------

}//WebGrep

#endif	/* CLIENT_HTTP_HPP */
