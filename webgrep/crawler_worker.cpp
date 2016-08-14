#include "crawler_worker.h"

#include <iostream>
#include <regex>
namespace WebGrep {

//---------------------------------------------------------------
bool CheckExtension(const char* buf, unsigned len)
{
  if (len < 4)
    {//slashes mean directories:
      return len > 1 && ('/' == buf[len - 1] || buf[0] == '/');
    }
  //not interesting files (media):
  static const char* d_file_extensions[ ] = {
      ".js",  ".jpg", "jpeg", ".mp4", "mpeg", ".avi",
      ".mkv", "rtmp", ".mov", ".3gp", ".wav", ".mp3",
      ".doc", ".odt", ".pdf", ".gif", ".ogv", ".ogg"
    };
  //interesting files:
  static const char* d_extensions[ ]
      = {"html", ".htm", ".asp", ".jsp", ".php", ".rb", ".prl", ".py"};
  const char* last4 = buf + len - 4;

  //since we have only 4 bytes there, lets compare them as uint32_t
  {
    uint32_t c = 0;
    const uint32_t* ptr1 = (const uint32_t*)last4;

    uint32_t sz = sizeof(d_file_extensions)/sizeof(const char*);
    const uint32_t* ptr2 = nullptr;
    for (ptr2 = (unsigned*)d_file_extensions[c];
         c < sz && *ptr1 != *ptr2;
         ++c, ptr2 = (const uint32_t*)d_file_extensions[c])
      {/*break on match*/  }
    if (c < sz)
      //got match with a media file, we're not interested
      return false;

    sz = sizeof(d_extensions)/sizeof(const char*);
    c = 0;
    for (ptr2 = (unsigned*)d_extensions[c];
         c < sz && *ptr1 != *ptr2;
         ++c, ptr2 = (const uint32_t*)d_extensions[c])
      {/*break on match*/  }

    //got match on interesting file extension
    if (c < sz)
      return true;
  }

  //if it contains at least 1 '.' -- then it's not our case
  bool canHazDot = false;
  for(uint32_t z = 0; z < 4; ++z)
    {
      canHazDot = canHazDot || ('.' == last4[z]);
    }
  //otherwise
  return !canHazDot && (0 == memcmp(buf, "http", 4) || ('/' == buf[0] || '/' == buf[len - 1]));
}
//---------------------------------------------------------------
size_t WorkerCtx::sheduleBranchExec(LinkedTask* node, WorkFunc_t method, uint32_t skipCount)
{
  LonelyTask sheep;
  sheep.action = method;
  sheep.ctx = *this;
  sheep.root = rootNode;
  sheep.target = node;

  return WebGrep::ForEachOnBranch(node, [this, &sheep](LinkedTask* node)
  { sheep.target = node; this->sheduleTask(&sheep);},
  skipCount);
}

/** shedule all all nodes of the branch to be executed by given functor.*/
size_t WorkerCtx::sheduleBranchExecFunctor(LinkedTask* task, std::function<void(LinkedTask*)> functor, uint32_t skipCount)
{
  return WebGrep::ForEachOnBranch(task, functor, skipCount);
}

//---------------------------------------------------------------
void PostProcHrefLinks(std::map<std::string, GrepVars::CIteratorPair>& out,
                       const std::smatch& matchURL,
                       GrepVars& g)
{
  std::string temp;
  temp.reserve(256);

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
      if (hrefPos == std::string::npos)
        {
          //case just http://
          auto page_pos = g.pageContent.begin() + matchURL.position(matchIdx);
          out[temp] = GrepVars::CIteratorPair(page_pos, page_pos + diff);
          continue;
        }
      for(; hrefPos < std::string::npos; hrefPos = temp.find_first_of(href, hrefPos + sizeof(href-1), sizeof(href-1)))
        {
          //case href
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
          out[temp] = GrepVars::CIteratorPair(begin, end);
        }
    }

}
//---------------------------------------------------------------

bool FuncDownloadOne(LinkedTask* task, WorkerCtx& w)
{
  GrepVars& g(task->grepVars);
  std::string& url(g.targetUrl);
  //try to connect, w.hostPort will be set on success to "site.com:443"
  g.scheme.fill(0);
  w.scheme.fill(0);

  w.hostPort = w.httpClient.connect(url);
  if (w.hostPort.empty())
    return false;
  w.scheme.copyFrom(w.httpClient.scheme());
  w.scheme.writeTo(g.scheme.data());

#ifndef NO_LIBNEON
  //issue GET request
  WebGrep::IssuedRequest rq = w.httpClient.issueRequest("GET", "/");
  //parse the results
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
  g.pageContent = std::move(rq.ctx->response);
  g.pageIsReady = true;
#else//case NO_LIBNEON
  //temporary solution for Windows: not using libneon, but QtNetwork instead
  //issue GET request
  WebGrep::IssuedRequest issue = w.httpClient.issueRequest("GET", "/");
  //the manager will dispatch asyncronously
  std::shared_ptr<QNetworkReply> rep = issue.ctx->makeGet(issue.req);
  std::unique_lock<std::mutex> lk(issue.ctx->mu);
  if (!rep->isFinished())
    {
      //wait for the reply, notification in Cli
      issue.ctx->cond.wait_for(lk, std::chrono::seconds(5));
    }
  //ok, got an reply
  g.responseCode = issue.ctx->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
  g.pageContent = std::move(issue.ctx->response);
  g.pageIsReady = !g.pageContent.empty();
#endif//NO_LIBNEON

  return g.pageIsReady;
}
//---------------------------------------------------------------
bool FuncGrepOne(LinkedTask* task, WorkerCtx& w)
{
  GrepVars& g(task->grepVars);
  g.pageIsParsed = false;
  g.pageIsReady = false;

  FuncDownloadOne(task, w);
  if (!g.pageIsReady || g.pageContent.empty())
    return false;

  /**TODO: figure out how much memory allocations std::regex or std::regex can
   * make while parsing the regular expressions.
   *  Maybe I should use PCRE or similar with C POD containers
   *  and non-singleton pool allocators
   *  (std::pool_allocator is a singleton, not good).
   *
 **/

  //used internally for sorting & duplicates removal
  std::map<std::string, GrepVars::CIteratorPair> matches;

  try
  {
    g.matchURLVector.clear();
    g.matchTextVector.clear();

    //grep the grepExpr within pageContent:
    std::smatch matchedText;
    bool gotText = std::regex_search(g.pageContent, matchedText, g.grepExpr);

    //grep the http:// URLs and spawn new nodes:
    for(std::regex& regexp : w.urlGrepExpressions)
      {
        std::smatch matchURL;
        if(std::regex_search(g.pageContent, matchURL, regexp))
          {
            PostProcHrefLinks(matches, matchURL, g);
          }
      }
    //---------------------------------------------------------------
    //put all together:
    //---------------------------------------------------------------

    {//-------- export section BEGIN ----------
      //push text match results into the vector
      for(size_t matchIdx = 0; gotText && matchIdx < matchedText.size(); ++matchIdx)
        {
          auto subitem = &(matchedText[matchIdx]);
          auto diff = subitem->second - subitem->first;
          auto begin = g.pageContent.begin() + matchedText.position(matchIdx);
          g.matchTextVector.push_back(GrepVars::CIteratorPair(begin, begin + diff));
        }
      auto rootNode = ItemLoadAcquire(task->root);
      if (nullptr == rootNode)
        rootNode = task;

      const std::string& rootTarget (rootNode->grepVars.targetUrl);
      static std::regex matchRoot("(:[0-9]*)/");
      //push matched URLS
      for(auto iter = matches.begin(); iter != matches.end(); ++iter)
        {
          const std::string& key((*iter).first);
          if (std::string::npos != key.find_first_of(rootTarget)
              && key.size() >= rootTarget.size()
              && std::regex_match(key, matchRoot))
            {
              continue;//skip link to main page
            }
          //filter which content we allow to be scanned:
          if (WebGrep::CheckExtension(key.data(), key.size())
              && key != g.targetUrl) //avoid scanning self again
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
    if (w.onException)
      {
        w.onException(ex.what());
      }
  }

  task->linksCounterPtr->fetch_add(g.matchURLVector.size());

  g.pageIsParsed = true;
  if (w.pageMatchFinishedCb)
    {
      w.pageMatchFinishedCb(task);
    }
  return g.pageIsReady && g.pageIsParsed;
}
//---------------------------------------------------------------
bool FuncDownloadGrepRecursive(LinkedTask* task, WorkerCtx& w)
{
  //case stopped by force:
  if (nullptr == task->linksCounterPtr)
    { return false; }

  size_t link_cnt = task->linksCounterPtr->load(std::memory_order_acquire);
  if (link_cnt >= task->maxLinksCountPtr->load(std::memory_order_acquire))
    {//max. links reached, lets stop the parsing
      if (w.onMaximumLinksCount) {
          w.onMaximumLinksCount(task);
        }
      return true;
    }
  //download one page:
  GrepVars& g(task->grepVars);
  //download and grep page for (text and URLs):
  if (!FuncGrepOne(task, w))
  {
    return false;
  }

  if (g.matchURLVector.empty())
    {
      //no URLS then no subtree items. but we're okay.
      return true;
    }
  LinkedTask* old = nullptr;
  LinkedTask* child = task->spawnChildNode(old); DeleteList(old);

  //create next level linked list from grepped URLS:
  size_t n_subtasks = child->spawnGreppedSubtasks(w.hostPort, task->grepVars);

  //emit signal that we've spawned a new level:
  if (0 != n_subtasks && nullptr != w.childLevelSpawned)
    {
      w.childLevelSpawned(child);
    }

  //start subtasks in different threads:
  std::cerr << __FUNCTION__ << " scheduling " << n_subtasks << " tasks more.\n";
  w.sheduleBranchExec(task, &FuncDownloadGrepRecursive, 1);
  return true;
}

//---------------------------------------------------------------
LonelyTask::LonelyTask() : target(nullptr), action(nullptr), additional(nullptr)
{

}

}//WebGrep
