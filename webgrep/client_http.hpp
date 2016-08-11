#ifndef CLIENT_HTTP_HPP
#define	CLIENT_HTTP_HPP

#include <string>
#include <memory>
#include <mutex>
#include <array>
#include "boost/noncopyable.hpp"
#ifndef _WIN32
extern "C" {
#include <neon/ne_session.h>
#include <neon/ne_request.h>
#include <neon/ne_utils.h>
#include <neon/ne_uri.h>
}
#else
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkReply>
#include <QtNetwork/QNetworkRequest>
#include <QObject>
#include <condition_variable>
#endif

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
#ifndef _WIN32
   std::shared_ptr<ne_request> req;
#else
   QNetworkRequest req;
#endif
   std::shared_ptr<ClientCtx> ctx; //holds reference of a context
  };

  Client();
  virtual ~Client();

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
#ifndef _WIN32
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
  //@return TRUE if scheme is "https"
  bool isHttps() const;

  ne_session* sess;
  std::array<char, 6> scheme;// "http\0\0" or "https\0"
  std::string response;
  std::string host_and_port;
  std::mutex mu;//locked in issueRequest()
};
#else //case Windows:
class ClientCtx : public QObject
{
  Q_OBJECT
public:
  explicit ClientCtx(QObject* p = nullptr);
  virtual ~ClientCtx() { }

  //@return TRUE if scheme is "https"
  bool isHttps() const;

public:
  std::string response;
  std::array<char, 6> scheme;// "http\0\0" or "https\0"
  uint16_t port;
  std::string host_and_port;

  std::mutex mu;//locked in issueRequest(), also used for condition variable
  std::condition_variable cond;

  std::shared_ptr<QNetworkReply> reply;
  QNetworkAccessManager* mgr;

private slots:
  //will invoke cond.notify_all();
  void replyFinished(QNetworkReply*);
};
#endif//_WIN32
//-----------------------------------------------------------------------------

}//WebGrep

#endif	/* CLIENT_HTTP_HPP */
