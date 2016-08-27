#include "crawler_worker.h"

#include <iostream>
#include <regex>

namespace WebGrep {

//---------------------------------------------------------------
bool CheckExtension(const char* buf, unsigned len)
{
  unsigned _pos = 0;
  unsigned _bound = std::min((unsigned)WebGrep::MaxURLlen,len);
  for(; _pos < _bound && buf[_pos] != '.'; ++_pos)
  { /*find a '.' symbol*/}

  if (_pos >= _bound)
    {//has not dots, consider it to be a folder
      return true;
    }
  //not interesting files (media, .css):
  static const char* d_file_extensions[ ] = {
      ".js",  ".jpg", "jpeg", ".mp4", "mpeg", ".avi",
      ".mkv", "rtmp", ".mov", ".3gp", ".wav", ".mp3",
      ".doc", ".odt", ".pdf", ".gif", ".ogv", ".ogg",
      ".css","\0\0\0\0"
    };
  //interesting files:
  static const char* d_extensions[ ]
      = {"html",".txt", ".cgi", ".htm", ".asp", ".jsp", ".php", ".rb", ".pl", ".py", "\0"};
  const char* last4 = buf + _pos;

  //since we have only 4 bytes there, lets compare them as uint32_t
  unsigned idx = 0;
  const char* ext = d_extensions[idx];
  uint32_t sz = sizeof(d_extensions)/sizeof(const char*) - 1;

  {//check for good extensions:
    for(; idx < sz && 0 == ::memcmp(last4, ext, ::strlen(ext));
        ++idx, ext = d_extensions[idx]) {  }
    if (idx < sz)
      return true;//matches recommended extensions
  }

  {//check for bad extenstions:
    idx = 0;
    ext = d_file_extensions[0];
    sz = sizeof(d_file_extensions)/sizeof(const char*) - 1;
    for(; idx < sz && 0 == ::memcmp(last4, ext, ::strlen(ext));
        ++idx, ext = d_extensions[idx]) {  }
    return (idx < sz)? false /*match with media extention*/ : true/*not match, return default(TRUE)*/;
  }
}
//---------------------------------------------------------------
size_t WorkerCtx::sheduleBranchExec(LinkedTask* node, WorkFunc_t method, uint32_t skipCount, bool spray)
{
  LonelyTask sheep;
  sheep.action = method;
  sheep.ctx = *this;
  sheep.root = rootNode;
  sheep.target = node;

  if(spray)
    {
      return WebGrep::ForEachOnBranch(node, [this, &sheep](LinkedTask* node)
        {
          sheep.target = node;
          this->sheduleTask(&sheep);
        },
      skipCount);
    }
  //else: make tasks consequent in one thread:
  this->sheduleFunctor
  ( [sheep, skipCount, node]()
    {
      LonelyTask ltask = sheep;
      WebGrep::ForEachOnBranch(node, [&ltask](LinkedTask* _node)
      {
          ltask.target = _node;
          ltask.action(_node, ltask.ctx);
      },
      skipCount);
    }
  );
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
      const uint32_t hrefSz = sizeof(href) - 1;
      size_t hrefPos = temp.find(href,0,4);
      if (hrefPos == std::string::npos)
        {
          //case just http://
          auto page_pos = g.pageContent.begin() + matchURL.position(matchIdx);
          out[temp] = GrepVars::CIteratorPair(page_pos, page_pos + diff);
          continue;
        }
      for(; hrefPos != std::string::npos;
          hrefPos = temp.find_first_of(href, hrefPos + hrefSz, hrefSz) )
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
  std::cerr << "downloading: " << url << "\n";

  //try to connect, w.hostPort will be set on success to "site.com:443"
  g.scheme.fill(0);
  w.scheme.fill(0);

  w.hostPort = w.httpClient.connect(url);
  if (w.hostPort.empty())
    return false;
  w.scheme.copyFrom(w.httpClient.scheme());
  w.scheme.writeTo(g.scheme.data());

  int readTimeOut = 2;//seconds
  if (nullptr == ItemLoadAcquire(task->parent))
    { //in case of root task -- we can increase the timeout
      readTimeOut = 8;
    }

  static const char* _defaultSlash = "/";
  size_t pathBegin  = url.find_first_of('/', FindURLAddressBegin(url.data(), url.size()));
  const char* _path = (std::string::npos == pathBegin)? _defaultSlash : url.data() + pathBegin;

#ifdef WITH_LIBNEON
  //issue GET request
  WebGrep::IssuedRequest rq = w.httpClient.issueRequest("GET", _path);
  ne_set_read_timeout(rq.ctx->sess, readTimeOut);
  //parse the results
  int result = ne_request_dispatch(rq.req.get());
  std::cerr << ne_get_error(rq.ctx->sess) << std::endl;
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
#elif defined(WITH_LIBCURL)
  WebGrep::IssuedRequest rq = w.httpClient.issueRequest("GET", _path);
  if (!rq.valid()) {
    return false;
    }

  curl_easy_setopt(rq.ctx->curl, CURLOPT_TIMEOUT, readTimeOut/*seconds*/);
  rq.res = curl_easy_perform(rq.ctx->curl);
  rq.ctx->status = rq.res;
  g.pageContent = std::move(rq.ctx->response);
  g.pageIsReady = (rq.res == CURLE_OK);
  rq.ctx->disconnect();


//end of WITH_LIBCURL

#elif defined(WITH_QTNETWORK)
  (void)pathBegin; (void)_path;
  //temporary solution for Windows: not using libneon, but QtNetwork instead
  //issue GET request
  WebGrep::IssuedRequest issue = w.httpClient.issueRequest("GET", url.data() + pathBegin);
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
//WITH_QTNETWORK
#endif//WITH_LIBNEON
  std::cerr << "download code: " << g.responseCode << "\n";

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
#if CRAWLER_WORKER_USE_REGEXP
    for(std::regex& regexp : w.urlGrepExpressions)
      {
        std::smatch matchURL;
        if(std::regex_search(g.pageContent, matchURL, regexp))
          {
            PostProcHrefLinks(matches, matchURL, g);
          }
      }
#else
    {
      const char hrefCSTR[] = "href";
      const char httpCSTR[] = "http";
      const uint32_t hrefSz = sizeof(hrefCSTR) - 1;
      const uint32_t httpSz = sizeof(httpCSTR) - 1;
      const std::string& _page(g.pageContent);

      for(size_t pos = 0; (pos + 1 + std::max(hrefSz,httpSz)) < _page.size(); ++pos)
        {
          size_t linkPos = pos;
          bool isHref = false;
          const char* ptr = _page.data() + pos;
          if (0 == ::memcmp(ptr, hrefCSTR, hrefSz))
            {
              //case href = "/resource/res2/page.html"
              linkPos = _page.find_first_of('=',4 + pos);
              isHref = true;
            }
          else if (0 == ::memcmp(ptr, httpCSTR, httpSz)
                   && (ptr[4] == ':' || (ptr[4] == 's')))
            {
              isHref = false;
            }
          else
            {
              continue;
            }

          //find opening quote:
          linkPos = _page.find_first_of('"', linkPos);
          linkPos++;
          //make new iterators that point to g.pageContent
          auto begin = _page.begin();
          begin += isHref? linkPos : pos;
          auto end = begin;

          //find closing quote \" or other character like '>'
          end += FindClosingQuote((const char*)&(*begin), _page.data()
                                    + std::min((size_t)WebGrep::MaxURLlen, _page.size()));

          if (end == _page.end() || end >= begin + WebGrep::MaxURLlen
              || (end - begin) <= 1
              || (!isHref && (begin + 10) > end )
              )
            {
              continue;
            }
          /**Make full path URL and pass to the matches map:
           * possible pairs:
           * (key: http://site.com/some/path/file.txt, value: "some/path/file.txt")
           * Why should we do it? key's full path is needed to avoid recursive calls when some links
           * in the page's text point to a path that has been scanned already. **/
          matches[MakeFullPath(&(*begin), end - begin, w.hostPort, g)] = GrepVars::CIteratorPair(begin, end);
          pos += (end - begin);
        }

    }

#endif
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
          bool extFilter = WebGrep::CheckExtension(key.data(), key.size()) && key != g.targetUrl;
          LinkedTask* _root = WebGrep::ItemLoadAcquire(task->root);

          bool traversalFilter = !(_root->grepVars.targetUrl == key);

          //traverse the tree up to the root and exclude current match
          //if it coincides with one of the parent nodes:
          LinkedTask* _node = WebGrep::ItemLoadAcquire(task->parent);
          for( ;
              nullptr != _node && _root != _node && traversalFilter;
              _node = WebGrep::ItemLoadAcquire(task->parent))
            {
              traversalFilter = traversalFilter && !(_node->grepVars.targetUrl == key);
            }

          if (extFilter && traversalFilter) //avoid scanning self again
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
      w.pageMatchFinishedCb(w.rootNode, task);
    }
  return g.pageIsReady && g.pageIsParsed;
}
//---------------------------------------------------------------
bool FuncDownloadGrepRecursive(LinkedTask* task, WorkerCtx& w)
{
  //case stopped by force:
  if (nullptr == task || nullptr == task->linksCounterPtr)
    { return false; }

  size_t link_cnt = task->linksCounterPtr->load(std::memory_order_acquire);
  if (link_cnt >= task->maxLinksCountPtr->load(std::memory_order_acquire))
    {//max. links reached, lets stop the parsing
      if (w.onMaximumLinksCount) {
          w.onMaximumLinksCount(w.rootNode, task);
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
  size_t n_subtasks = child->spawnGreppedSubtasks(w.hostPort, g, 0);

  //emit signal that we've spawned a new level:
  if (nullptr != w.childLevelSpawned)
    {
      w.childLevelSpawned(w.rootNode, child);
    }

  //call self by sending tasks calling this method to different threads
  //(tasks ventillation)
  std::cerr << __FUNCTION__ << " scheduling " << n_subtasks << " tasks more.\n";
  w.sheduleBranchExec(child, &FuncDownloadGrepRecursive, 1);
  return true;
}

//---------------------------------------------------------------
LonelyTask::LonelyTask() : target(nullptr), action(nullptr), additional(nullptr)
{

}

}//WebGrep
