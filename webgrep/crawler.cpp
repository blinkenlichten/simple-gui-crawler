#include "crawler.h"
#include <iostream>
#include "crawler_worker.h"
#include <atomic>
#include <mutex>
#include <vector>
#include <thread>
#include "boost/lockfree/queue.hpp"
#include "boost/thread/thread_pool.hpp"

namespace WebGrep {

class Crawler::CrawlerPV
{
public:
  CrawlerPV()
  {
    firstPageTask = nullptr;
    maxLinksCount.store(4096);
    currentLinksCount.store(0);

    onMainSubtaskCompleted = [this](LinkedTask* task, WorkerCtxPtr)
    {
      std::lock_guard<std::mutex> lk(wlistMutex); (void)lk;
      std::cout << "subtask completed: " << task->grepVars.targetUrl << "\n";
      std::cout << "subtask content: \n" << task->grepVars.pageContent << "\n";
    };
  }

  virtual ~CrawlerPV()
  {
    clear();
  }

  void clear()
  {
    std::lock_guard<std::mutex> lk(wlistMutex); (void)lk;
    if (nullptr != workersPool)
      workersPool->join();
    destroyList(firstPageTask);
    delete firstPageTask;
    firstPageTask = nullptr;
    currentLinksCount.store(0);
  }

  static void destroyList(LinkedTask* head)
  {
    if (nullptr == head)
      return;
    for(LinkedTask* next = ItemLoadAcquire(head->next);
        nullptr != next; next = ItemLoadAcquire(head->next))
      {
        destroyList(next);
      }
    destroyList(ItemLoadAcquire(head->child));
  }
  LinkedTask* firstPageTask;

  std::mutex wlistMutex;
  std::unique_ptr<boost::executors::basic_thread_pool> workersPool;
  std::vector<WorkerCtxPtr> workContexts;
  std::atomic_uint maxLinksCount, currentLinksCount;

  std::function<void(LinkedTask*, WorkerCtxPtr)> onMainSubtaskCompleted;
};
//---------------------------------------------------------------
Crawler::Crawler()
{
  pv.reset(new CrawlerPV);
  onException = [] (const std::string& what){ std::cerr << what << "\n"; };
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

  LinkedTask*& root(pv->firstPageTask);

  try {
    setMaxLinks(maxLinks);
    setThreadsNumber(threadsNum);
    std::lock_guard<std::mutex> lk(pv->wlistMutex); (void)lk;
    for(WorkerCtxPtr& ctx : pv->workContexts)
      {
        ctx->running = true;
      }
    //delete old nodes:
    if (nullptr != root && url != root->grepVars.targetUrl)
      {
        pv->destroyList(root);
        pv->firstPageTask = nullptr;
      }
    //alloc toplevel node:
    root = new LinkedTask;
    root->linksCounterPtr = &(pv->currentLinksCount);
    root->maxLinksCountPtr = &(pv->maxLinksCount);
    GrepVars& g(root->grepVars);
    g.targetUrl = url;
    g.grepExpr = grepRegex;
  } catch(std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    this->onException(e.what());
    return false;
  }

  //just picking up first worker context
  auto worker = pv->workContexts[0];

  //submit a root-task: get the first page and then follow it's content's links in new threads
  auto fn = [this, worker]() {
      //this functor will wake-up pending task from another thread
      FuncGrepOne(pv->firstPageTask, worker);
      pv->firstPageTask->spawnGreppedSubtasks(worker->hostPort);
      std::string temp;
      GrepVars& g(pv->firstPageTask->grepVars);
      for(size_t cnt = 0; cnt < g.matchURLVector.size(); ++cnt)
        {
          temp.assign(g.matchURLVector[cnt].first, g.matchURLVector[cnt].second);
          std::cerr << temp << std::endl;
        }

      //ventillate subtasks:
      std::lock_guard<std::mutex> lk(pv->wlistMutex); (void)lk;
      size_t item = 0;

      //rearrange first task's spawned subitems to different independent contexts:
      for(auto node = WebGrep::ItemLoadAcquire(pv->firstPageTask->child);
          nullptr != node;
          ++item %= pv->workContexts.size(), node = WebGrep::ItemLoadAcquire(node->next) )
        {
          auto ctx = pv->workContexts[item];
          //submit recursive grep for each 1-st level subtask:
          pv->workersPool->submit([node, ctx]
                                  (){ FuncDownloadGrepRecursive(node, ctx); });
        }
    };
  pv->workersPool->submit(fn);
  return true;
}

void Crawler::stop()
{
  std::lock_guard<std::mutex> lk(pv->wlistMutex); (void)lk;
  for(WorkerCtxPtr& ctx : pv->workContexts)
    {
      ctx->running = false;
    }
}

void Crawler::setMaxLinks(unsigned maxScanLinks)
{
  //sync with load(acquire)
  pv->maxLinksCount.store(maxScanLinks, std::memory_order_release);
}
//---------------------------------------------------------------
void Crawler::setThreadsNumber(unsigned nthreads)
{
  if (0 == nthreads)
    {
      std::cerr << "void Crawler::setThreadsNumber(unsigned nthreads) -> 0 value ignored.\n";
      return;
    }
  {
    std::lock_guard<std::mutex> lk(pv->wlistMutex); (void)lk;
    for(WorkerCtxPtr& wp : pv->workContexts)
      { wp->running = false; }
    if (nullptr != pv->workersPool)
      {
        pv->workersPool->close();
        pv->workersPool->join();
      }

    pv->workContexts.resize(nthreads);
    for(WorkerCtxPtr& wp : pv->workContexts)
      {
        if (nullptr == wp)
          wp = std::make_shared<WorkerCtx>();
      }
  }
  boost::executors::basic_thread_pool* newPool = new boost::executors::basic_thread_pool(nthreads);
  pv->workersPool.reset(newPool);
 }
//---------------------------------------------------------------

}//WebGrep
