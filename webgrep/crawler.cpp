#include "crawler.h"
#include <iostream>
#include "crawler_worker.h"
#include <atomic>
#include <mutex>
#include <vector>
#include "boost/lockfree/queue.hpp"

namespace WebGrep {

class Crawler::CrawlerPV
{
public:
  CrawlerPV()
  {
    workers.reserve(16);
    workers.push_back(std::make_shared<WebGrep::Worker>());
    maxLinksCount.store(4096);

    onMainSubtaskCompleted = [this](LinkedTask* task, WorkerPtr)
    {
      std::lock_guard<std::mutex> lk(wlistMutex); (void)lk;
      std::cout << "subtask completed: " << task->grepVars.targetUrl << "\n";
      std::cout << "subtask content: \n" << task->grepVars.pageContent << "\n";
    };
  }

  virtual ~CrawlerPV()
  {
    std::lock_guard<std::mutex> lk(wlistMutex); (void)lk;
    for(std::shared_ptr<WebGrep::Worker>& val : workers)
      {
        val->stop();
      }
    destroyList(firstPageTask);
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
    delete head;
  }
  LinkedTask* firstPageTask;

  std::mutex wlistMutex;
  std::vector<std::shared_ptr<WebGrep::Worker>> workers;
  std::atomic_uint maxLinksCount;

  std::function<void(LinkedTask*, WorkerPtr)> onMainSubtaskCompleted;
};
//---------------------------------------------------------------
Crawler::Crawler()
{
  pv.reset(new CrawlerPV);
}
//---------------------------------------------------------------
bool Crawler::start(const std::string& url,
                    const std::string& grepRegex,
                    unsigned maxLinks, unsigned threadsNum)
{
  try {
    setMaxLinks(maxLinks);
    setThreadsNumber(threadsNum);
    std::lock_guard<std::mutex> lk(pv->wlistMutex); (void)lk;
    //delete old nodes:
    if (url != pv->firstPageTask->grepVars.targetUrl)
      {
        pv->destroyList(pv->firstPageTask);
        pv->firstPageTask = nullptr;
      }
    //alloc toplevel node:
    pv->firstPageTask = new LinkedTask;
    GrepVars& g(pv->firstPageTask->grepVars);
    g.targetUrl = url;
    g.grepExpr = grepRegex;
  } catch(std::exception& e)
  {
    std::cerr << e.what() << std::endl;
    return false;
  }

  WorkerCommand cmd;
  //first task will grep one page and distribute subtasks to other work threads
  cmd.command =  WorkerAction::GREP_ONE;
  cmd.task = pv->firstPageTask;
  cmd.taskDisposer = [this](LinkedTask* task, WorkerPtr w)
  {
    //rearrange first task subitems to other threads:
    std::lock_guard<std::mutex> lk(pv->wlistMutex); (void)lk;
    size_t item = 0;
    for(auto nextLevelTree = WebGrep::ItemLoadAcquire(task->child);
        nullptr != nextLevelTree;
        ++item %= pv->workers.size(), nextLevelTree = WebGrep::ItemLoadAcquire(nextLevelTree->next) )
      {
        WorkerCommand subcmd;
        subcmd.command = WorkerAction::DOWNLOAD_AND_GREP_RECURSIVE;
        subcmd.task = nextLevelTree;
        subcmd.taskDisposer = pv->onMainSubtaskCompleted;
        pv->workers[item]->put(subcmd);
      }
  };
  pv->workers[0]->put(cmd);
  return true;
}

void Crawler::pause()
{
  for(std::shared_ptr<WebGrep::Worker>& val : pv->workers)
    {
      val->stop();
    }
}

void Crawler::stop()
{
  pause();
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
  std::lock_guard<std::mutex> lk(pv->wlistMutex); (void)lk;

  typedef std::vector<WebGrep::WorkerCommand> CmdVector_t;
  std::vector<CmdVector_t> taskLeftOvers;

  if (nthreads < pv->workers.size())
    {
      taskLeftOvers.resize(pv->workers.size() - nthreads);
      for(unsigned cnt = 0; cnt < nthreads; ++cnt)
        {
          taskLeftOvers[cnt] = (pv->workers[cnt])->stop();
        }
    }
  pv->workers.resize(nthreads);
  for(std::shared_ptr<WebGrep::Worker>& val : pv->workers)
    {
      if (nullptr == val)
        val = std::make_shared<WebGrep::Worker>();
      val->start();
    }
  //put leftover tasks into other threads
  for(size_t item = taskLeftOvers.size(); item > 0; )
    {
      --item;
      CmdVector_t& vec(taskLeftOvers[item]);
      pv->workers[item % (pv->workers.size() - 1)]->put(&vec[0], vec.size());
    }
}
//---------------------------------------------------------------

}//WebGrep
