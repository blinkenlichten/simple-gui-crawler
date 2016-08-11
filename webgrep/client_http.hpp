#ifndef CLIENT_HTTP_HPP
#define	CLIENT_HTTP_HPP

#ifndef _WIN32
    #include "ch_ctx_nix.h"
#else
    #include "ch_ctx_win32.h"
#endif

/** Modified version from https://github.com/eidheim/Simple-Web-Server */
namespace WebGrep {

std::string ExtractHostPortHttp(const std::string& targetUrl);

//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------
/** Contains connection context and it's dependant request tasks,
 * the destructor will clean up it all.
*/
class Client
{
public:
  Client();
  virtual ~Client() { }

  /** Connect to a host, use issueRequest() when connected.
   * Thread-safe: shared_ptr<ClientCtx> is constructed on each connect.
   * @return extracted host and port string like "site.com:443"
   * or empty string on fail.*/
  std::string connect(const std::string& httpURL);

  //can be used concurrentthread-safe locking(default is not)
  IssuedRequest issueRequest(const char* method, const char* path,
                             bool withLock = false);

  //returns 0-terminated "http" or "https" or NULL if not connected
  const char* scheme() const;
  //returns port for connection or 0 if not initialized
  uint16_t port() const;
protected:
  std::shared_ptr<ClientCtx> ctx;//not null when connected
};
//-----------------------------------------------------------------------------
//-----------------------------------------------------------------------------

}//WebGrep

#endif	/* CLIENT_HTTP_HPP */
