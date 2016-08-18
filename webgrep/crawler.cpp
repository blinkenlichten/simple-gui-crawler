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

  std::shared_ptr<LinkedTask> mainTask;

  try {
    setMaxLinks(maxLinks);
    if (nullptr == pv->taskRoot || pv->taskRoot->grepVars.targetUrl != url)
      {
        //alloc toplevel node:
        mainTask.reset(new LinkedTask,
                       [](LinkedTask* ptr){WebGrep::DeleteList(ptr);});

        pv->currentLinksCount->store(0);
        mainTask->linksCounterPtr = (pv->currentLinksCount);
        mainTask->maxLinksCountPtr = (pv->maxLinksCount);
      }
    else
      {
        mainTask = pv->taskRoot;
      }

    GrepVars* g = &(mainTask->grepVars);
    g->targetUrl = url;
    g->grepExpr = grepRegex;

    //submit a root-task:
    //get the first page and then follow it's content's links in new threads.
    //It is to be done async. to avoid GUI lags etc.
    pv->workersPool->submit([crawlerImpl, threadsNum, mainTask]()
    { /*async start.*/
        crawlerImpl->start(mainTask, threadsNum);
    });
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
    pv->start(pv->taskRoot, nthreads);

  } catch(const std::exception& ex)
  {
    std::cerr << ex.what() << std::endl;
    if(pv->onException) { pv->onException(ex.what()); }
  }
 }
//---------------------------------------------------------------

}//WebGrep
