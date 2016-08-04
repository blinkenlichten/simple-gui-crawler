#include "crawler_worker.h"

#include <iostream>
#include "boost/regex.hpp"
#include "boost/pool/pool_alloc.hpp"
#include "boost/asio/ssl/context_base.hpp"

namespace WebGrep {

//---------------------------------------------------------------
//---------------------------------------------------------------
bool FuncDownloadOne(LinkedTask* task, WorkerCtxPtr w)
{
  GrepVars& g(task->grepVars);
  std::string& url(g.targetUrl);
  w->hostPort = w->httpClient.connect(url);
  if(w->hostPort.empty())
    return false;

  Client::IssuedRequest rq = w->httpClient.issueRequest("GET", "/");
  int result = ne_request_dispatch(rq.req.get());
  std::cerr << ne_get_error(rq.ctx->sess);
  if (NE_OK != result)
    {
      return false;
    }

  g.responseCode = ne_get_status(rq.req.get())->code;

  if (200 != g.responseCode)
    return false;

  g.pageContent = rq.ctx->response;
//  std::cerr << g.pageContent << std::endl;
  g.pageIsReady = true;
  return true;
}
//---------------------------------------------------------------
bool FuncGrepOne(LinkedTask* task, WorkerCtxPtr w)
{
  GrepVars& g(task->grepVars);
  if (!g.pageIsReady)
    {
      FuncDownloadOne(task, w);
    }
  if (!g.pageIsReady || g.pageContent.empty())
    return false;

  //grep the grepExpr:
  boost::regex_search(g.pageContent, g.matchedText, g.grepExpr);
  //grep the http:// URLs and spawn new nodes:
  boost::regex_search(g.pageContent, g.matchURL, WebGrep::HttpExrp);
  g.pageIsParsed = true;
  if (task->pageMatchFinishedCb)
    {
      task->pageMatchFinishedCb(task, w);
    }
  return g.pageIsReady && g.pageIsParsed;
}
//---------------------------------------------------------------
bool FuncDownloadGrepRecursive(LinkedTask* task, WorkerCtxPtr w)
{
  //case stopped by force:
  if (nullptr == task->linksCounterPtr || !w->running)
    {
      return false;
    }

  //max. links reached, lets stop the parsing
  if (task->maxLinkCount <= task->linksCounterPtr->load(std::memory_order_acquire))
    {
      w->onMaximumLinksCount(task, w);
      return true;
    }
  //download one page:
  GrepVars& g(task->grepVars);
  //download and grep page for (text and URLs):
  FuncGrepOne(task, w);
  if (!g.pageIsParsed)
    return false;

  if (g.matchURL.empty())
    {
      //no URLS then no subtree items.
      return true;
    }

  //create next level linked list from grepped URLS:
  size_t n_subtasks = task->spawnGreppedSubtasks();

  //emit signal that we've spawned a new level:
  if (0 != n_subtasks && nullptr != task->childLevelSpawned)
    {
      task->childLevelSpawned(ItemLoadAcquire(task->child), w);
    }

  //for each child node of subtree: schedule tasks to parse them too
  auto child = ItemLoadAcquire(task->child);
  for(; nullptr != child; child = ItemLoadAcquire(child->next))
    {
      w->sheduleTask([child, w](){FuncDownloadGrepRecursive(child, w);});
    }
   return true;
}

//---------------------------------------------------------------

}//WebGrep
