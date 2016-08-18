#include "crawler_private.h"

namespace WebGrep {

bool CrawlerPV::selfTest() const
{
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
    return false;
  }
  return true;
}
//--------------------------------------------------------------
void CrawlerPV::start(std::shared_ptr<LinkedTask> neuRootTask, unsigned threadsNumber, bool forceRebuild)
{
  if (neuRootTask.get() != taskRoot.get())
    {//stop ASAP with tasks termination
      workersPool->terminateDetach();
    }
  else
    {//stop temporarly, with tasks re-sheduling
      stop();
    }

  //set up workersPool if needed.
  if (workersPool->closed() || workersPool->threadsCount() != threadsNumber)
    {
      workersPool.reset(new WebGrep::ThreadsPool(threadsNumber));
    }

  if(taskRoot == neuRootTask)
  { //submit previously abandoned tasks due to stop()
    std::lock_guard<std::mutex> lk(slockLonelyFunctors); (void)lk;
    if (!lonelyFunctorsVector.empty())
      {
        workersPool->submit(&lonelyFunctorsVector[0], lonelyFunctorsVector.size());
      }
  }

  taskRoot = neuRootTask;

  WorkerCtx worker = makeWorkerContext();
  if (taskRoot->grepVars.pageIsParsed && !forceRebuild)
    { //do not re-parse everything if it's ready unless forced to
      return;
    }
  //Start parsing the first(root's) URL
  //this functor will wake-up pending task from another thread
  FuncGrepOne(taskRoot.get(), worker);
  LinkedTask* expell = nullptr;

  //make a child node for new sequence of pages for download/grep
  LinkedTask* child = taskRoot->spawnChildNode(expell); DeleteList(expell);
  size_t spawnedCnt = child->spawnGreppedSubtasks(worker.hostPort, taskRoot->grepVars, 0);
  std::cerr << "Root task: " << spawnedCnt << " spawned;\n";
  worker.childLevelSpawned(child);
  //we have grepped N URLs from the first page
  //ventillate them as subtasks:
  worker.sheduleBranchExec(child, &FuncDownloadGrepRecursive, 0 );

}
//--------------------------------------------------------------
void CrawlerPV::stop()
{
  auto this_shared = shared_from_this();

  std::function<void(WebGrep::CallableDoubleFunc&)> exportFn =
      [this,this_shared](WebGrep::CallableDoubleFunc& dfunc)
  {
      std::lock_guard<std::mutex> lk(this_shared->slockLonelyFunctors);
      (void)lk;
      this_shared->lonelyFunctorsVector.push_back(dfunc.functor);
  };
  //terminate the tasks manager and export abandoned tasks here:
  auto workersCopy = workersPool;
  std::thread waiter([this, this_shared, workersCopy, &exportFn](){
      if (nullptr == workersCopy)
        { return; }
      try {
        workersCopy->joinExportAll(exportFn);
      } catch(...)
      { std::cerr << "Error! Failed to join workers thread!\n";
      }
    });
  waiter.detach();


}
//--------------------------------------------------------------
void CrawlerPV::clear()
{
  stop();
  taskRoot.reset();
  currentLinksCount->store(0);
  {
    std::lock_guard<std::mutex> lk(slockLonely);
    lonelyVector.clear();
  }
  std::lock_guard<std::mutex>lk(slockLonelyFunctors);
  lonelyFunctorsVector.clear();
}
//---------------------------------------------------------------
WorkerCtx CrawlerPV::makeWorkerContext()
{
  WorkerCtx ctx;
  ctx.rootNode = taskRoot;

  auto crawlerImpl = shared_from_this();
  //enable workers to spawn subtasks e.g. "start"
  ctx.pageMatchFinishedCb  = [crawlerImpl](LinkedTask* node)
  {
    if (crawlerImpl->onPageScanned)
      crawlerImpl->onPageScanned(crawlerImpl->taskRoot, node);
  };

  ctx.childLevelSpawned = ctx.pageMatchFinishedCb;

  ctx.sheduleTask = [crawlerImpl](const LonelyTask* task)
  {
      crawlerImpl->sheduleTask(*task);
  };

  ctx.sheduleFunctor = [crawlerImpl](CallableFunc_t func)
  {
    crawlerImpl->sheduleFunctor(func);
  };
  return ctx;
}
//---------------------------------------------------------------
bool CrawlerPV::sheduleTask(const WebGrep::LonelyTask& task)
{
  try {
    if (workersPool->closed())
      {//shedule abandoned task to a vector while we're managing threads:
        std::lock_guard<CrawlerPV::LonelyLock_t> lk(slockLonely); (void)lk;
        lonelyVector.push_back(task);
        return true;
      }
    //submit current task:
    workersPool->submit([task](){
        WorkerCtx ctx_copy = task.ctx;
        task.action(task.target, ctx_copy);
      });

    //pull out and submit previously abandoned tasks:
    std::lock_guard<CrawlerPV::LonelyLock_t> lk(slockLonely); (void)lk;
    for(const LonelyTask& alone : lonelyVector)
      {
        workersPool->submit([alone](){
            LonelyTask sheep = alone;
            sheep.action(sheep.target, sheep.ctx);
          });
      }
    lonelyVector.clear();
  } catch(std::exception& ex)
  {
    std::cerr << "Exception: " << __FUNCTION__ << "  \n";
    if (onException) { onException(ex.what()); }
    return false;
  }
  return true;

}
//-----------------------------------------------------------------
bool CrawlerPV::sheduleFunctor(CallableFunc_t func)
{
  try {
    if (workersPool->closed())
      {//workers pool is unavailable, lets stack tasks in the vector
        std::lock_guard<CrawlerPV::LonelyLock_t> lk(slockLonelyFunctors); (void)lk;
        lonelyFunctorsVector.push_back(func);
        return true;
      }
    //submit current task:
    workersPool->submit(func);

    //submit delayed tasks from the vector:
    std::lock_guard<CrawlerPV::LonelyLock_t> lk(slockLonelyFunctors); (void)lk;
    for(CallableFunc_t& func : lonelyFunctorsVector)
      {
        workersPool->submit(std::move(func));
      }
    lonelyFunctorsVector.clear();
  } catch(std::exception& ex)
  {
    std::cerr << "Exception: " << __FUNCTION__ << "  \n";
    if (onException) { onException(ex.what()); }
    return false;
  }
  return true;
}


}//namespace WebGrep
