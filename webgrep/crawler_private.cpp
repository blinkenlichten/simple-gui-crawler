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
void CrawlerPV::start()
{
  auto root = taskRoot;
  WorkerCtx worker = makeWorkerContext();

  //this functor will wake-up pending task from another thread
  FuncGrepOne(root.get(), worker);
  size_t spawnedCnt = root->spawnGreppedSubtasks(worker.hostPort, root->grepVars);
  std::cerr << "Root task: " << spawnedCnt << " spawned;\n";

  //ventillate subtasks:
  worker.sheduleBranchExec(root.get(), &FuncDownloadGrepRecursive, 1/*skip root task*/ );

}
//--------------------------------------------------------------
void CrawlerPV::stopThreads()
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
  ctx.sheduleTask = [crawlerImpl](const LonelyTask* task)
  { crawlerImpl->sheduleTask(*task); };

  ctx.sheduleFunctor = [crawlerImpl](CallableFunc_t func)
  { crawlerImpl->sheduleFunctor(func);};
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
