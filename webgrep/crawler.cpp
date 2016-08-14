#include "crawler.h"
#include "crawler_private.h"

namespace WebGrep {


//---------------------------------------------------------------
void Crawler::setExceptionCB(OnExceptionCallback_t func)
{
  pv->onException = func;
}

void Crawler::setPageScannedCB(OnPageScannedCallback_t func)
{
  pv->onPageScanned = func;
}

Crawler::Crawler()
{
  pv = std::make_shared<CrawlerPV>();
  pv->onException = [] (const std::string& what){ std::cerr << what << "\n"; };
}
void Crawler::clear()
{
  pv->clear();
}
//---------------------------------------------------------------
bool Crawler::start(const std::string& url,
                    const std::string& grepRegex,
                    unsigned maxLinks, unsigned threadsNum)
{
  //----------------------------------------------------------------
  // These functors are set in a way that binds them to reference-counted
  // private implementation object that can be swapped.
  // Purpose: to be able to join the threads in a detached thread,
  // we don't need any delays in the main loop.
  //----------------------------------------------------------------
  auto crawlerImpl = pv;
  //----------------------------------------------------------------
  //----------------------------------------------------------------

  std::shared_ptr<LinkedTask>& root(pv->taskRoot);

  try {
    stop();
    setMaxLinks(maxLinks);
    setThreadsNumber(threadsNum);
    if (nullptr == root || root->grepVars.targetUrl != url)
      {
        pv->taskRoot = nullptr;
        //alloc toplevel node:
        root.reset(new LinkedTask,
                   [](LinkedTask* ptr){WebGrep::DeleteList(ptr);});

        pv->currentLinksCount->store(0);
        root->linksCounterPtr = (pv->currentLinksCount);
        root->maxLinksCountPtr = (pv->maxLinksCount);
      }

    GrepVars* g = &(root->grepVars);
    g->targetUrl = url;
    g->grepExpr = grepRegex;

    //submit a root-task:
    //get the first page and then follow it's content's links in new threads.
    //It is done async. to avoid GUI lags etc.
    pv->sheduleFunctor([crawlerImpl](){ crawlerImpl->start(); });
  } catch(const std::exception& ex)
  {
    std::cerr << ex.what() << "\n";
    if(pv->onException)
      { pv->onException(ex.what()); }
    return false;
  }
  return true;
}

void Crawler::stop()
{
  pv->stop();
}

void Crawler::setMaxLinks(unsigned maxScanLinks)
{
  //sync with load(acquire)
  pv->maxLinksCount->store(maxScanLinks);
}
//---------------------------------------------------------------
void Crawler::setThreadsNumber(unsigned nthreads)
{
  if (0 == nthreads)
    {
      std::cerr << "void Crawler::setThreadsNumber(unsigned nthreads) -> 0 value ignored.\n";
      return;
    }
  try {
    pv->stopThreads();
    pv->workersPool.reset(new boost::basic_thread_pool(nthreads));

  } catch(const std::exception& ex)
  {
    std::cerr << ex.what() << std::endl;
    if(pv->onException) {
      pv->onException(ex.what());
    }
  }
 }
//---------------------------------------------------------------

}//WebGrep
