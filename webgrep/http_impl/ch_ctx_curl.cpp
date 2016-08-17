#include "ch_ctx_curl.h"
#include <cstring>
#include <functional>


namespace WebGrep {
typedef std::function<void()> Ftor_t;
static std::shared_ptr<Ftor_t> CurlGlobalCleaner;

ClientCtx::ClientCtx()
  : curl(nullptr), port(0)
{
  scheme.fill(0x00);
  status = CURL_LAST;

  static std::once_flag curl_once;
  std::call_once(curl_once, []() {
      curl_global_init(CURL_GLOBAL_ALL);
      CurlGlobalCleaner = std::make_shared<Ftor_t>
          ([](){curl_global_cleanup(); });
    });

  curl = curl_easy_init();
  url.reserve(256);
}

ClientCtx::~ClientCtx()
{
  if (nullptr != curl)
    curl_easy_cleanup(curl);
}

bool ClientCtx::isHttps() const
{
  return (0 == ::memcmp(scheme.data(), "https", 5));
}

}//WebGrep
