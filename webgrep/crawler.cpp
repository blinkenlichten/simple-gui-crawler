#include "crawler.h"
#include <iostream>
#include "crawler_worker.h"
#include <atomic>
#include <vector>
#include <thread>
#include "boost/lockfree/queue.hpp"
#include "boost/thread/thread_pool.hpp"
#include <stdio.h>
#include <cstdlib>
#include <cassert>

namespace WebGrep {

class Crawler::CrawlerPV
{
public:
  CrawlerPV()
  {
    maxLinksCount = std::make_shared<std::atomic_uint>();
    currentLinksCount = std::make_shared<std::atomic_uint>();

    maxLinksCount->store(4096);
    currentLinksCount->store(0);
    workersPool = std::make_shared<boost::executors::basic_thread_pool>(4);

    std::cerr << __FUNCTION__ << " self test: \n";
    try {
      for(unsigned z = 0; z < 4; ++z)
        {
          std::shared_ptr<WebGrep::LinkedTask> lq;
          lq.reset(new LinkedTask,
                   [](LinkedTask* ptr){WebGrep::DeleteList(ptr);});
          LinkedTask* tmp = 0;
          auto child = lq->spawnChildNode(tmp);
          size_t n = child->spawnNextNodes(1024 * z + z);
          assert(n == (1024 * z + z));
        }
      std::cerr << "OKAY\n";
    } catch(std::exception& ex) {
      std::cerr << " FAILED with exception: "
               << ex.what() << "\n";
    }
    lonelyVector.reserve(2 * Crawler::maxThreads);
    lonelyFunctorsVector.reserve(2 * Crawler::maxThreads);
  }

  virtual ~CrawlerPV()
  {
    try {
      clear();
    } catch(...)
    { }
  }

  void stopThreads()
  {
    stop();
    workersPool->close();
    auto workersCopy = workersPool;

    std::thread waiter([this, workersCopy](){
        if (nullptr == workersCopy)
          { return; }
        try {
          workersCopy->join();
        } catch(...)
        { std::cerr << "Error! Failed to join workers thread!\n";
        }
      });
    waiter.detach();
  }

  void clear()
  {
    stopThreads();
    taskRoot.reset();
    currentLinksCount->store(0);
  }

  void stop()
  {

  }

  Crawler::OnExceptionCallback_t onException;
  Crawler::OnPageScannedCallback_t onPageScanned;

  //main task (for first html page) all other are subtasks
  std::shared_ptr<LinkedTask> taskRoot;

  std::shared_ptr<boost::executors::basic_thread_pool> workersPool;

  //we certainly won't need this to be dynamically resized:
  std::array<WorkerCtx,Crawler::maxThreads> jobContextArray;

  //these shared by all tasks spawned by the object CrawlerPV:
  std::shared_ptr<std::atomic_uint> maxLinksCount, currentLinksCount;

  //---- these variables track for abandoned tasks that are to be re-issued:
  // these provide sync. access to lonelyVector, lonelyFunctorsVector
  typedef boost::detail::spinlock LonelyLock_t;
  boost::detail::spinlock slockLonely, slockLonelyFunctors;
  std::vector<WebGrep::LonelyTask> lonelyVector;
  std::vector<WebGrep::WorkerCtx::CallableFunc_t> lonelyFunctorsVector;
};
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
  //this functor serves as sheduling method for objects LonelyTask (Task pointers basically)
  auto shedulingTask = [crawlerImpl](WebGrep::LonelyTask task)
  {
    if(task.root.get() != crawlerImpl->taskRoot.get())
      return;
    if (crawlerImpl->workersPool->closed())
      {//shedule abandoned task to a vector while we're managing threads:
        std::lock_guard<CrawlerPV::LonelyLock_t> lk(crawlerImpl->slockLonely); (void)lk;
        crawlerImpl->lonelyVector.push_back(task);
        return;
      }
    //normal sheduling way:
    crawlerImpl->workersPool->submit([task](){ task.action(task.sheduledTask, task.ctx); });

    //pull out and submit previously abandoned tasks:
    std::lock_guard<CrawlerPV::LonelyLock_t> lk(crawlerImpl->slockLonely); (void)lk;
    for(const LonelyTask& alone : crawlerImpl->lonelyVector)
      {
        crawlerImpl->workersPool->submit([alone](){ alone.action(alone.sheduledTask, alone.ctx); });
      }
    crawlerImpl->lonelyVector.clear();
  };
  //----------------------------------------------------------------
  //this functor services as sheduling method for callable function objects
  auto shedulingFunctor = [crawlerImpl](WorkerCtx::CallableFunc_t func)
  {
    if (crawlerImpl->workersPool->closed())
      {
        std::lock_guard<CrawlerPV::LonelyLock_t> lk(crawlerImpl->slockLonelyFunctors); (void)lk;
        crawlerImpl->lonelyFunctorsVector.push_back(func);
        return;
      }
    crawlerImpl->workersPool->submit(func);
    std::lock_guard<CrawlerPV::LonelyLock_t> lk(crawlerImpl->slockLonelyFunctors); (void)lk;
    for(WorkerCtx::CallableFunc_t&& func : crawlerImpl->lonelyFunctorsVector)
      {
        crawlerImpl->workersPool->submit(std::move(func));
      }
    crawlerImpl->lonelyFunctorsVector.clear();
  };
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

    //----------------------------------------------------------------
    //set the callbacks that will be used by the tasks
    for(WorkerCtx& ctx : pv->jobContextArray)
      {
        //enable workers to spawn subtasks e.g. "start"
        ctx.pageMatchFinishedCb  = [crawlerImpl](LinkedTask* node)
        {
          if (crawlerImpl->onPageScanned)
            crawlerImpl->onPageScanned(crawlerImpl->taskRoot, node);
        };
        ctx.sheduleTask = shedulingTask;
        ctx.sheduleFunctor = shedulingFunctor;
      }
  } catch(std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    if (pv->onException)
      pv->onException(e.what());
    return false;
  }

  //just picking up first worker context
  auto worker = pv->jobContextArray.data();

  //submit a root-task: get the first page and then follow it's content's links in new threads
  auto fn = [this, worker]() {
      WebGrep::LinkedTask* root = pv->taskRoot.get();
      //this functor will wake-up pending task from another thread
      FuncGrepOne(root, *worker);
      size_t spawnedCnt = root->spawnGreppedSubtasks(worker->hostPort, root->grepVars);
      std::cerr << "Root task: " << spawnedCnt << " spawned;\n";

      //ventillate subtasks:
      size_t item = 0;

      WebGrep::ForEachOnBranch(pv->taskRoot.get(),
                               [this, &item](LinkedTask* node, void*)
      {
          //submit recursive grep for each 1-st level subtask to different workers:
          auto ctx = pv->jobContextArray.data() + (item++ % pv->jobContextArray.size());
          pv->workersPool->submit([node, ctx]
                                  (){ FuncDownloadGrepRecursive(node, /*copy WorkerCtx*/*ctx); });
      }, false/*skip root*/);
    };
  try {
    pv->workersPool->submit(fn);
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
