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

  g.matchURLVector.clear();
  //grep the grepExpr:
  boost::regex_search(g.pageContent, g.matchedText, g.grepExpr);

  {//grep the http:// URLs and spawn new nodes:
    try
    {

      std::string temp;
      temp.reserve(256);

      //match href="URL"
      boost::regex_search(g.pageContent, w->matchURL, w->hrefGrepExpr);
      for(size_t matchIdx = 0; matchIdx < w->matchURL.size(); ++matchIdx)
        {
          auto subitem = &(w->matchURL[matchIdx]);
          auto diff = subitem->second - subitem->first;
          temp.resize(diff);
          auto shift = w->matchURL.position(matchIdx);
          auto matchPageBegin = g.pageContent.begin() + shift;
          temp.assign(matchPageBegin, matchPageBegin + shift);

          const char href[] = "href";
          size_t hrefPos = temp.find(href,0,4);

          if (hrefPos < temp.size())
            {//case href
              auto quotePos = temp.find_first_of('=',hrefPos);
              quotePos = temp.find_first_of('"', quotePos);
              quotePos++;
              //make new iterators that point to g.pageContent
              auto begin = matchPageBegin + quotePos;
              auto end = begin;
              auto quote2 = temp.find_first_of('"', quotePos);
              if (quote2 != std::string::npos)
                end += (size_t)(quote2 - quotePos);
              temp.assign(begin, end);
              w->matches[temp] = GrepVars::CIteratorPair(begin, end);
            }
          else
            {//case just http://
              auto page_pos = g.pageContent.begin() + w->matchURL.position(matchIdx);
              w->matches[temp] = GrepVars::CIteratorPair(page_pos, page_pos + diff);
            }
        }
      //match http://
      boost::regex_search(g.pageContent, w->matchHttp, w->urlGrepExpr);
      //put all together:
      for(auto iter = w->matches.begin(); iter != w->matches.end(); ++iter)
        {
          std::string& key((*item).first);
          //filter which content we allow to be scanned:
          if (key.size() > 1
              && key != g.targetUrl //avoid scanning self again
              && (key[key.size() - 1]) == "/"
              || std::string::npos != key.find_last_of(".htm")
              || std::string::npos != key.find_last_of(".asp")
              || std::string::npos != key.find_last_of(".php")
              )
            {
              g.matchURLVector.push_back((*iter).second);
            }
        }

    }
    catch(const std::exception& ex)
    {
      std::cerr << ex.what() << std::endl;
    }
  }

  task->linksCounterPtr->fetch_add(g.matchURLVector.size());

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
  size_t cnt = task->linksCounterPtr->load(std::memory_order_acquire);
  if (cnt >= task->maxLinksCountPtr->load(std::memory_order_acquire))
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

  if (g.matchURLVector.empty())
    {
      //no URLS then no subtree items.
      return true;
    }

  //create next level linked list from grepped URLS:
  size_t n_subtasks = task->spawnGreppedSubtasks(w->hostPort);

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
