#include "thread_pool.h"
#include "linked_task.h"

namespace WebGrep {
//=============================================================================
bool ThreadsPool::performSelfTest()
{
  std::atomic_uint g_cnt;
  g_cnt.store(0);

  try {
    ThreadsPool pool(4);
    std::shared_ptr<WebGrep::LinkedTask> ltask;
    ltask.reset(new WebGrep::LinkedTask,
                [](void* ptr){WebGrep::DeleteList((WebGrep::LinkedTask*)ptr);}
    );
    size_t spawned = ltask->spawnNextNodes(99);

    class LList : public std::enable_shared_from_this<LList>
    {
    public:
      explicit LList(std::atomic_uint& ref, uint32_t i = 0) : idx(i)
      {
        dfunc.functor = [this, &ref](){ ;
            std::cerr << "working item " << idx << std::endl;
            ref.fetch_add(1);
            throw std::logic_error("just checking reaction for fake error...");
          };
        dfunc.cbOnException = [this](const std::exception& ex)
        {
            std::cerr << "idx: " << idx << " " << ex.what() << std::endl;
          };
      }
      uint32_t idx;
      WebGrep::CallableDoubleFunc dfunc;
      std::shared_ptr<LList> next;
    };
    std::shared_ptr<LList> head = std::make_shared<LList>(g_cnt, 0);
    std::shared_ptr<LList> ptr = head;
    uint32_t cnt = 0;
    WebGrep::ForEachOnBranch(ltask.get(),
                             [&ptr, &cnt, &g_cnt](LinkedTask*)
    {
        ptr->next = std::make_shared<LList>(g_cnt, ++cnt);
        ptr = ptr->next;
      }, 0);

    ptr = head;

    WebGrep::IteratorFunc_t ifunc = [&ptr](WebGrep::CallableDoubleFunc** pptr, size_t* pcnt) -> bool
    {
      (void)pcnt;
      if (nullptr != ptr->next)
        {
          *pptr = &(ptr->dfunc);
          ptr = ptr->next;
          return true;
        }
      return false;
    };
    pool.submit(&(ptr->dfunc), 0, ifunc, false);
    std::thread t([&pool](){pool.joinAll();});
    t.join();
  } catch(std::exception& ex)
  {
    std::cerr << __FUNCTION__ << " test failed: " << ex.what() << std::endl;
    return false;
  }

  auto value = g_cnt.load();
  return 101 == value;
}
//=============================================================================

ThreadsPool::ThreadsPool(uint32_t nthreads)
  : d_closed(false)
{
  threadsVec.resize(nthreads);
  mcVec.resize(nthreads);
  size_t idx = 0;

  for(ThreadPtr& t : threadsVec)
    {
      mcVec[idx] = std::make_shared<ThreadData>();
      ThreadDataPtr data = mcVec[idx];
      auto lamb = [data, this]()
      {
          this->processingLoop(data);
      };//lambda

      t = std::make_shared<std::thread>(lamb);
      idx++;
    }
}

void ThreadsPool::processingLoop(const ThreadDataPtr& td)
{
  std::vector<CallableDoubleFunc> localArray;
  localArray.reserve(32);

  while(!td->stopFlag)
    {
      std::unique_lock<std::mutex> lk(td->mu);
      td->cond.wait(lk);
      //move task queue to local array to do not keep the mutex much
      localArray = std::move(td->workQ);
      lk.unlock();

      for(CallableDoubleFunc& pair: localArray)
        {
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
    }//while
}

size_t ThreadsPool::threadsCount() const
{
    return threadsVec.size();
}

bool ThreadsPool::closed() const
{
    return d_closed;
}

bool ThreadsPool::submit(const WebGrep::CallableFunc_t& ftor)
{
  if (closed())
    return false;
  CallableDoubleFunc pair;
  pair.functor = ftor;
  pair.cbOnException = [](const std::exception& ex)
  { std::cerr << "Exception: " << ex.what() << std::endl;};
  return submit(&pair, 1);
}

bool ThreadsPool::submit(CallableDoubleFunc* ftorArray, size_t len, IteratorFunc_t iterFn, bool spray)
{
  if (closed())
    return false;

  d_current.fetch_add(len, std::memory_order_acquire);
  unsigned idx = d_current.load(std::memory_order_relaxed) % threadsVec.size();
  ;

  try {
    if (!spray)
      {//case we serialize tasks just to 1 thread
        ThreadDataPtr& td(mcVec[idx]);
        {
          std::unique_lock<std::mutex>lk(td->mu);

          CallableDoubleFunc* ptr = ftorArray;
          size_t cnt = len - 1;

          bool ok = true;
          for(; ok ; ok = iterFn(&ptr, &cnt))
            {
              td->workQ.push_back(*ptr);
            }
        }
        td->cond.notify_all();
        return true;
      }

    //case we serialize tasks to all threads (spraying them):
    CallableDoubleFunc* ptr = ftorArray;
    size_t cnt = len - 1;
    bool ok = true;
    for(; ok; ok = iterFn(&ptr, &cnt))
      {
        ThreadDataPtr& td(mcVec[idx]);
        {
          std::unique_lock<std::mutex>lk(td->mu);
          td->workQ.push_back(*ptr);
        }
        td->cond.notify_all();
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

void ThreadsPool::joinAll()
{
  std::lock_guard<std::mutex> lk(joinMutex);
  if (threadsVec.empty())
    return;

  close();
  for(ThreadDataPtr& dt : mcVec)
    {
      dt->stopFlag = true;
      dt->cond.notify_all();
    }
  for(ThreadPtr& t : threadsVec)
    {
      t->join();
    }
  threadsVec.clear();
  mcVec.clear();
}

}//WebGrep
