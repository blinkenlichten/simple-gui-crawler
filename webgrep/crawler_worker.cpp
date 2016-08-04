#include "crawler_worker.h"

#include <iostream>
#include "boost/regex.hpp"
#include "boost/pool/pool_alloc.hpp"
#include "boost/asio/ssl/context_base.hpp"

namespace WebGrep {

static std::string ExtractHostPortHttp(const std::string& targetUrl)
{
  std::string url(targetUrl.data());
  size_t hpos = url.find_first_of("://");
  if (std::string::npos != hpos)
    {
      url = targetUrl.substr(hpos + sizeof("://")-1);
    }
  auto slash_pos = url.find_first_of('/');
  if (std::string::npos != slash_pos)
    {
      url.resize(slash_pos);
    }
  return url;
}
//---------------------------------------------------------------
//---------------------------------------------------------------
bool FuncDownloadOne(LinkedTask* task, WorkerCtxPtr w)
{
  std::shared_ptr<SimpleWeb::Client>& hclient(w->httpClient);
  GrepVars& g(task->grepVars);
  std::string& url(g.targetUrl);
  w->httpConfig.host_port = ExtractHostPortHttp(url).data();
  w->httpConfig.isHttps = (std::string::npos != g.targetUrl.find_first_of("https://"));

  if (nullptr == hclient)
    {
      hclient = std::make_shared<SimpleWeb::Client>(w->httpConfig);
    }
  else
    {
      hclient->connect(w->httpConfig.host_port);
    }
  std::shared_ptr<SimpleWeb::Response> response = hclient->request("GET", "/");
  if (SimpleWeb::Response::STATUS::OKAY != response->status)
    {
      return false;
    }
  //copy the page
  response->content >> task->grepVars.pageContent;
  char* temp = nullptr;
  task->grepVars.responseCode = ::strtol(response->status_code.data(),&temp, 10);
  return 200 == task->grepVars.responseCode;
}
//---------------------------------------------------------------
bool FuncGrepOne(LinkedTask* task, WorkerCtxPtr w)
{
  GrepVars& g(task->grepVars);
  if (g.pageContent.empty())
    {
      FuncDownloadOne(task, w);
    }
  if (g.pageContent.empty())
    return false;

  //grep the grepExpr:
  boost::regex_search(g.pageContent, g.matchedText, g.grepExpr);
  //grep the http:// URLs and spawn new nodes:
  boost::regex_search(g.pageContent, g.matchURL, WebGrep::HttpExrp);
  g.pageIsReady = true;
  if (task->pageMatchFinishedCb)
    {
      task->pageMatchFinishedCb(task, w);
    }
  return g.pageIsReady;
}
//---------------------------------------------------------------
bool FuncDownloadGrepRecursive(LinkedTask* task, WorkerCtxPtr w)
{
  if (task->maxLinkCount <= task->linksCounterPtr->load(std::memory_order_acquire))
    {
      w->onMaximumLinksCount(task, w);
      return true;
    }
  if (nullptr == task->linksCounterPtr || !w->isRunning())
    {
      return false;
    }
  //download one page:
  FuncDownloadOne(task, w);
  GrepVars& g(task->grepVars);
  if (g.pageContent.empty())
    {
      return false;
    }
  //grep page for (text and URLs):
  FuncGrepOne(task, w);
  if (g.matchURL.empty())
    {
      //no URLS then no subtree items.
      return true;
    }

  //create next level linked list from grepped URLS:
  {
    auto child = new LinkedTask;
    { //fill fields of 1st child node
      child->shallowCopy(*task);
      child->parent = task;
      child->level = 1u + task->level;
      std::stringstream out(child->grepVars.targetUrl);
      out << g.matchURL[0];
    }
    //put child to task.child
    task->childNodesCount.fetch_add(1, std::memory_order_release);
    task->child.store((std::uintptr_t)child, std::memory_order_release);
    //process next children: (one for each URL)

    //spawn items on same level (access by .next)
    for(unsigned cnt = 1; cnt < g.matchURL.size() - 1;
        --cnt, child = ItemLoadAcquire(child->next))
      {
        auto item = new LinkedTask;
        item->shallowCopy(*child);
        std::stringstream out(item->grepVars.targetUrl);
        out << g.matchURL[cnt];
        item->parent = task;
        task->childNodesCount.fetch_add(1, std::memory_order_release);
        child->next.store((std::uintptr_t)item, std::memory_order_release);
      }
    //emit signal that we've spawned a new level:
    if (task->childLevelSpawned)
      {
        task->childLevelSpawned(ItemLoadAcquire(task->child), w);
      }
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
