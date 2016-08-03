#include "client_http.hpp"

namespace SimpleWeb {

Client::Client(AsioSrvPtr asio, const std::string& host_port, unsigned short default_port)
  : asio_io_service(asio), asio_resolver(*asio_io_service), socket_error(false)
{
  cachedResponse.reset(new Response());
  cachedResponse->status_code.reserve(64);
  cachedResponse->http_version.reserve(8);
  setHost(host_port, default_port);

  asio_endpoint = boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), port);
  socket = std::make_shared<TCPSocketType>(*asio_io_service);
}

void Client::setHost(const std::string& host_port, unsigned short default_port)
{
  size_t host_end = host_port.find(':');
  if(host_end == std::string::npos) {
      host = host_port;
      port = default_port;
    }
  else {
      host = host_port.substr(0, host_end);
      port = static_cast<unsigned short>(stoul(host_port.substr(host_end+1)));
    }
}

void Client::connect(std::string hostPort)
{
  bool reconnect = false;
  if(!hostPort.empty() && std::string::npos == hostPort.find_first_of(host))
    {
      setHost(hostPort);
      reconnect = true;
    }
  if( reconnect || socket_error || !socket->is_open()) {
      boost::asio::ip::tcp::resolver::query query(host, std::to_string(port));
      boost::asio::connect(*socket, asio_resolver.resolve(query));

      boost::asio::ip::tcp::no_delay option(true);
      socket->set_option(option);

      socket_error=false;
    }
}


std::shared_ptr<Response> Client::request
(const char* request_type,
 const char* path,
 boost::string_ref content,
 const std::map<std::string, std::string>& header )
{
  corrected_path = path;
  if(corrected_path == "")
    corrected_path = "/";

  std::ostream write_stream(&write_buffer);
  write_stream.write(request_type, strlen(request_type));
  write_stream << " " << corrected_path << " HTTP/1.1\r\n";
  write_stream << "Host: " << host << "\r\n";
  for(auto& h: header) {
      write_stream << h.first << ": " << h.second << "\r\n";
    }
  if(content.size() > 0)
    write_stream << "Content-Length: " << content.size() << "\r\n";
  write_stream << "\r\n";

  try {
    connect();

    boost::asio::write(*socket, write_buffer);
    if(content.size() > 0)
      boost::asio::write(*socket, boost::asio::buffer(content.data(), content.size()));

  }
  catch(const std::exception& e) {
    socket_error=true;
    throw std::invalid_argument(e.what());
  }

  return request_read();
}

std::shared_ptr<Response> Client::request(const char* request_type,
                                          const char* path,
                                          std::iostream& content,
                                          const std::map<std::string, std::string>& header)
{
  corrected_path = path;
  if(corrected_path == "")
    corrected_path = "/";

  content.seekp(0, std::ios::end);
  auto content_length = content.tellp();
  content.seekp(0, std::ios::beg);

  std::ostream write_stream(&write_buffer);
  write_stream.write(request_type, strlen(request_type));
  write_stream << " " << corrected_path << " HTTP/1.1\r\n";
  write_stream << "Host: " << host << "\r\n";
  for(auto& h: header) {
      write_stream << h.first << ": " << h.second << "\r\n";
    }
  if(content_length > 0)
    write_stream << "Content-Length: " << content_length << "\r\n";
  write_stream << "\r\n";
  if(content_length > 0)
    write_stream << content.rdbuf();

  try {
    this->connect();

    boost::asio::write(*socket, write_buffer);
  }
  catch(const std::exception& e) {
    socket_error=true;
    throw std::invalid_argument(e.what());
  }

  return request_read();
}


void Client::parse_response_header(std::shared_ptr<Response> response, std::istream& stream) const {
  std::string line;
  getline(stream, line);
  size_t version_end=line.find(' ');
  if(version_end!=std::string::npos) {
      if(5<line.size())
        response->http_version=line.substr(5, version_end-5);
      if((version_end+1)<line.size())
        response->status_code=line.substr(version_end+1, line.size()-(version_end+1)-1);

      getline(stream, line);
      size_t param_end;
      while((param_end=line.find(':'))!=std::string::npos) {
          size_t value_start=param_end+1;
          if((value_start)<line.size()) {
              if(line[value_start]==' ')
                value_start++;
              if(value_start<line.size())
                response->header.insert(std::make_pair(line.substr(0, param_end), line.substr(value_start, line.size()-value_start-1)));
            }

          getline(stream, line);
        }
    }
}

std::shared_ptr<Response> Client::request_read()
{
  std::shared_ptr<Response>& response(cachedResponse);

  try {
    size_t bytes_transferred = boost::asio::read_until(*socket, response->content_buffer, "\r\n\r\n");

    size_t num_additional_bytes=response->content_buffer.size()-bytes_transferred;

    parse_response_header(response, response->content);

    auto header_it=response->header.find("Content-Length");
    if(header_it!=response->header.end()) {
        auto content_length=stoull(header_it->second);
        if(content_length > num_additional_bytes) {
            boost::asio::read(*socket, response->content_buffer,
                              boost::asio::transfer_exactly(content_length-num_additional_bytes));
          }
      }
    else if((header_it=response->header.find("Transfer-Encoding"))!=response->header.end() && header_it->second=="chunked") {
        boost::asio::streambuf streambuf;
        std::ostream content(&streambuf);

        std::streamsize length;
        std::string buffer;
        do {
            size_t bytes_transferred = boost::asio::read_until(*socket, response->content_buffer, "\r\n");
            std::string line;
            getline(response->content, line);
            bytes_transferred-=line.size()+1;
            line.pop_back();
            length=stol(line, 0, 16);

            auto num_additional_bytes=static_cast<std::streamsize>(response->content_buffer.size()-bytes_transferred);

            if((2+length)>num_additional_bytes) {
                boost::asio::read(*socket, response->content_buffer,
                                  boost::asio::transfer_exactly(2+length-num_additional_bytes));
              }

            buffer.resize(static_cast<size_t>(length));
            response->content.read(&buffer[0], length);
            content.write(&buffer[0], length);

            //Remove "\r\n"
            response->content.get();
            response->content.get();
          } while(length>0);

        std::ostream response_content_output_stream(&response->content_buffer);
        response_content_output_stream << content.rdbuf();
      }
  }
  catch(const std::exception& e) {
    socket_error=true;
    throw std::invalid_argument(e.what());
  }

  return response;
}
//-------------------------

}//SimpleWEB

