#include "ch_ctx_nix.h"

namespace WebGrep {

ClientCtx::~ClientCtx()
{
  if (nullptr != sess)
    ne_session_destroy(sess);
}

bool ClientCtx::isHttps() const
{
  return (0 == ::memcmp(scheme.data(), "https", 5));
}

}//WebGrep
