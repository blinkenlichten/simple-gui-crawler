#include "thread_pool.h"
#include "linked_task.h"
#include <list>
#include <chrono>

namespace WebGrep {
struct Maker
{
  Maker(const TPool_ThreadDataPtr& data) : pos(0), dataPtr(data)
  {
    localArray.reserve(32);
  }
  ~Maker()
  {
    //
    try {
      //case we have to export abandoned tasks:
      if (nullptr != dataPtr->exportTaskFn)
        {
          for(size_t k = pos; k < localArray.size(); ++k)
            { dataPtr->exportTaskFn(localArray[k]); }
          //move and export tasks that are left there:
          std::unique_lock<std::mutex> lk(dataPtr->mu);
          pull(dataPtr);
          for(size_t k = 0; k < localArray.size(); ++k)
            { dataPtr->exportTaskFn(localArray[k]); }

        } else if (!dataPtr->terminateFlag)
        {//finish the jobs left there:
          exec(dataPtr->terminateFlag);
          std::unique_lock<std::mutex> lk(dataPtr->mu);
          pull(dataPtr);
          exec(dataPtr->terminateFlag);
        }
    } catch(std::exception& ex)
    {
      std::cerr << ex.what() << std::endl;
    }

  }
  void pull(const TPool_ThreadDataPtr& td)
  {
    //move task queue to local array
    localArray = std::move(td->workQ);
    pos = 0;
    td->workQ.clear();
  }
  void exec(volatile bool& term_flag)
  {
    size_t k = pos;
    for(; k < localArray.size() && !term_flag; ++k)
      {
        CallableDoubleFunc& pair(localArray[k]);
        try {
          if (nullptr != pair.functor)
            pair.functor();
        }
        catch(const std::exception& ex)
        {
          if (pair.cbOnException)
            pair.cbOnException(ex);
        }
      }//for

    pos = k;
  }
  TPool_ThreadDataPtr dataPtr;
  size_t pos;//position
  std::vector<CallableDoubleFunc> localArray;

};

void ThreadsPool_processingLoop(const TPool_ThreadDataPtr& td)
{
  Maker taskM(td);

  while(!td->stopFlag)
    {
      std::unique_lock<std::mutex> lk(td->mu);
      td->cond.wait(lk);
      taskM.pull(td);
      taskM.exec(td->terminateFlag);
    }//while

  //the dtor() will either execute or export unfinished jobs
}


ThreadsPool::ThreadsPool(uint32_t nthreads)
  : d_closed(false)
{
  threadsVec.resize(nthreads);
  mcVec.resize(nthreads);

  size_t idx = 0;

  for(std::thread& t : threadsVec)
    {
      mcVec[idx] = std::make_shared<TPool_ThreadData>();
      t = std::thread(ThreadsPool_processingLoop, mcVec[idx]);
      idx++;
    }
}

size_t ThreadsPool::threadsCount() const
{
    return threadsVec.size();
}

bool ThreadsPool::closed() const
{
    return d_closed;
}
bool ThreadsPool::submit(CallableDoubleFunc& ftor)
{
  if (closed())
    return false;
  return submit(&ftor, 1);
}
bool ThreadsPool::submit(const WebGrep::CallableFunc_t& ftor)
{
  if (closed())
    return false;
  CallableDoubleFunc pair;
  pair.functor = ftor;
  pair.cbOnException = [](const std::exception& ex)
  { std::cerr << "Exception suppressed: " << ex.what() << std::endl;};
  return submit(&pair, 1);
}

bool ThreadsPool::submit(CallableDoubleFunc* ftorArray, size_t len, IteratorFunc_t iterFn, bool spray)
{
  if (closed())
    return false;

  size_t inc = std::max((size_t)1, len);
  d_current.fetch_add(inc, std::memory_order_acquire);
  unsigned idx = d_current.load(std::memory_order_relaxed) % threadsVec.size();

  try {
    if (!spray)
      {//case we serialize tasks just to 1 thread
        TPool_ThreadData* td = mcVec[idx].get();
        {
          std::unique_lock<std::mutex>lk(td->mu);

          CallableDoubleFunc* ptr = ftorArray;

          bool ok = true;
          for(size_t cnt = 0; ok; ok = iterFn(&ptr, &cnt, len))
            {
              td->workQ.push_back(*ptr);
            }
        }
        td->cond.notify_all();
        return true;
      }

    //case we serialize tasks to all threads (spraying them):
    CallableDoubleFunc* ptr = ftorArray;
    bool ok = true;
    for(size_t cnt = 0; ok; ok = iterFn(&ptr, &cnt, len))
      {
        TPool_ThreadData* td = mcVec[idx].get();
        {
          std::unique_lock<std::mutex>lk(td->mu);
          (void)lk;
          td->workQ.push_back(*ptr);
        }
        td->cond.notify_all();
        d_current.fetch_add(inc, std::memory_order_acquire);
        idx = d_current.load(std::memory_order_relaxed) % threadsVec.size();
      }
  } catch(...)
  {
    return false;//on exception like bad_alloc
  }

  return true;
}

void ThreadsPool::close()
{
    d_closed = true;
}

bool ThreadsPool::joined()
{
  std::lock_guard<std::mutex> lk(joinMutex);
  return threadsVec.empty();
}

void ThreadsPool::joinAll(bool terminateCurrentTasks)
{
  std::lock_guard<std::mutex> lk(joinMutex);
  if (threadsVec.empty())
    return;

  close();
  for(TPool_ThreadDataPtr& dt : mcVec)
    {
      dt->terminateFlag = terminateCurrentTasks;
      dt->stopFlag = true;
      dt->cond.notify_all();
    }

  for(std::thread& t : threadsVec)
    {
      t.join();
    }
  threadsVec.clear();
  mcVec.clear();
}

void ThreadsPool::joinExportAll(std::function<void(CallableDoubleFunc&)>& exportFunctor)
{
  {//set the exporing callback
    std::lock_guard<std::mutex> lk(joinMutex);
    if (threadsVec.empty())
      return;

    close();
    for(TPool_ThreadDataPtr& dt : mcVec)
      {
        dt->exportTaskFn = exportFunctor;
        dt->stopFlag = true;
      }
  }
  //terminate tasks, they'll export abandoned exec. functor
  joinAll(true);
}

}//WebGrep
