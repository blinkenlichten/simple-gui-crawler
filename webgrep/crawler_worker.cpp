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

  switch (g.responseCode)
    {
    //redirecting:
    case 301:

    case 302:
      {
        const char* location = ne_get_response_header(rq.req.get(),"Location");
        if (nullptr == location)//failed to get Location header
          { return false; }
        url = location;
        return FuncDownloadOne(task, w);
      };
      break;
    case 200: break;

    default: {return false;};
    };

  g.pageContent = rq.ctx->response;
  g.pageIsReady = true;
  return true;
}
//---------------------------------------------------------------
bool FuncGrepOne(LinkedTask* task, WorkerCtxPtr w)
{
  GrepVars& g(task->grepVars);
  g.pageIsParsed = false;

  if (!g.pageIsReady)
    {
      FuncDownloadOne(task, w);
    }
  if (!g.pageIsReady || g.pageContent.empty())
    return false;

  /**TODO: figure out how much memory allocations boost::regex or std::regex can
   * make while parsing the regular expressions.
   *  Maybe I should use PCRE or similar with C POD containers
   *  and non-singleton pool allocators
   *  (boost::pool_allocator is a singleton, not good).
   *
 **/

  //used internally for sorting & duplicates removal
  std::map<std::string, GrepVars::CIteratorPair> matches;

  try
  {
    g.matchURLVector.clear();
    g.matchTextVector.clear();

    //grep the grepExpr within pageContent:
    boost::smatch matchedText;
    boost::regex_search(g.pageContent, matchedText, g.grepExpr);

    //grep the http:// URLs and spawn new nodes:
    std::string temp;
    temp.reserve(256);

    boost::smatch matchURL;

    //match href="URL"
    boost::regex_search(g.pageContent, matchURL, w->hrefGrepExpr);
    for(size_t matchIdx = 0; matchIdx < matchURL.size(); ++matchIdx)
      {
        auto subitem = &(matchURL[matchIdx]);
        auto diff = subitem->second - subitem->first;
        temp.resize(diff);
        auto shift = matchURL.position(matchIdx);
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
            matches[temp] = GrepVars::CIteratorPair(begin, end);
          }
        else
          {//case just http://
            auto page_pos = g.pageContent.begin() + matchURL.position(matchIdx);
            matches[temp] = GrepVars::CIteratorPair(page_pos, page_pos + diff);
          }
      }

    //---------------------------------------------------------------
    //put all together:
    //---------------------------------------------------------------

    {//-------- export section BEGIN ----------
      //push text match results into the vector
      for(size_t matchIdx = 0; matchIdx < matchedText.size(); ++matchIdx)
        {
          auto subitem = &(matchedText[matchIdx]);
          auto diff = subitem->second - subitem->first;
          auto begin = g.pageContent.begin() + matchedText.position(matchIdx);
          g.matchTextVector.push_back(GrepVars::CIteratorPair(begin, begin + diff));
        }


    //push matched URLS
    for(auto iter = matches.begin(); iter != matches.end(); ++iter)
      {
        const std::string& key((*iter).first);
        //filter which content we allow to be scanned:
        if (key.size() > 1
            && key != g.targetUrl //avoid scanning self again
            &&
            ( (key[key.size() - 1]) == '/'
              || std::string::npos != key.find_last_of(".htm")
              || std::string::npos != key.find_last_of(".asp")
              || std::string::npos != key.find_last_of(".php")
              ) )
          {
            g.matchURLVector.push_back((*iter).second);
          }
      }
    //end grep http

    }//--------- export section END ----------

  }
  catch(const std::exception& ex)
  {
    std::cerr << ex.what() << std::endl;
    if (w->onException)
      {
        w->onException(task, w, ex.what());
      }
  }

  task->linksCounterPtr->fetch_add(g.matchURLVector.size());

  g.pageIsParsed = true;
  if (w->pageMatchFinishedCb)
    {
      w->pageMatchFinishedCb(task, w);
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
  if (0 != n_subtasks && nullptr != w->childLevelSpawned)
    {
      w->childLevelSpawned(ItemLoadAcquire(task->child), w);
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
