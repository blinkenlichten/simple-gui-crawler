#ifndef CLIENT_HTTP_HPP
#define	CLIENT_HTTP_HPP

#include <boost/asio.hpp>
#include <boost/utility/string_ref.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <boost/functional/hash.hpp>

#include <boost/pool/pool_alloc.hpp>
#include <boost/smart_ptr/detail/spinlock.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>

#include <unordered_map>
#include <map>
#include <random>

/** Modified version from https://github.com/eidheim/Simple-Web-Server */

namespace SimpleWeb {

typedef boost::asio::ssl::stream<boost::asio::ip::tcp::socket> ssl_socket;
typedef boost::asio::ip::tcp::socket socket_ip;
typedef std::shared_ptr<boost::asio::io_service> AsioSrvPtr;

class Response;
//---------------------------------------------------------------
struct ClientConfig
{
  ClientConfig() : isHttps(true),default_port(443)
  {  }
  AsioSrvPtr asio;
  std::shared_ptr<boost::asio::ssl::context> asio_context;

  //URL: http://host.com:8080 or https://host.com:443 or without https://
  std::string host_port;
  bool isHttps;
  unsigned short default_port;
};
//---------------------------------------------------------------

/** At the moment it support only SSL socket,
 * it'll have to be templated back as in SimpleWebServer project ... later.
*/
class Client : public std::enable_shared_from_this<Client>
{
public:

  /** @param host port:  somesite.com:8080/uri */
  Client(ClientConfig cfg);
  virtual ~Client() {}

  virtual void connect(std::string hostPort = "");
  std::shared_ptr<Response> request(const char* request_type,
                                    const char* path,
                                    boost::string_ref content="",
                                    const std::map<std::string, std::string>& header=std::map<std::string, std::string>());

  std::shared_ptr<Response> request(const char* request_type,
                                    const char* path,
                                    std::iostream& content,
                                    const std::map<std::string, std::string>& header=std::map<std::string, std::string>());

  boost::asio::streambuf write_buffer;

  std::string host;
  unsigned short port;

protected:
  void setHost(const std::string& host_port, unsigned short default_port = 443);

  ClientConfig config;
  boost::asio::ip::tcp::endpoint asio_endpoint;
  boost::asio::ip::tcp::resolver asio_resolver;
  bool isHttps;

  std::shared_ptr<SimpleWeb::ssl_socket> socket;
  bool socket_error;


  void parse_response_header(std::shared_ptr<Response> response, std::istream& stream) const;

  std::shared_ptr<Response> cachedResponse;
  std::shared_ptr<Response> request_read();
  std::string corrected_path;
};

//-------------------------
class Response {
  friend class Client;

  //Based on http://www.boost.org/doc/libs/1_60_0/doc/html/unordered/hash_equality.html
  class iequal_to {
  public:
    bool operator()(const std::string &key1, const std::string &key2) const {
      return boost::algorithm::iequals(key1, key2);
    }
  };
  class ihash {
  public:
    size_t operator()(const std::string &key) const {
      std::size_t seed=0;
      for(auto &c: key)
        boost::hash_combine(seed, std::tolower(c));
      return seed;
    }
  };
public:
  enum STATUS { OKAY, SOCKET_ERROR};
  STATUS status;
  std::string http_version, status_code;
  std::istream content;
  std::unordered_multimap<std::string, std::string, ihash, iequal_to> header;

private:
  boost::asio::streambuf content_buffer;

  Response(): status(STATUS::OKAY), content(&content_buffer)
  {

  }
};

}//namespace SimpleWeb

#endif	/* CLIENT_HTTP_HPP */
