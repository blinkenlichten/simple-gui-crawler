#ifndef THREAD_BOOL_H
#define THREAD_BOOL_H

#include <thread>
#include <mutex>
#include <memory>
#include <condition_variable>
#include <atomic>
#include <vector>
#include "noncopyable.hpp"

namespace WebGrep {

typedef std::function<void()> CallableFunc_t;

/** encapsulates a callable and an exception callback.*/
struct CallableDoubleFunc
{
  CallableDoubleFunc() { }
  CallableDoubleFunc(const WebGrep::CallableFunc_t& f)
    : functor(f) {}

  WebGrep::CallableFunc_t functor;
  std::function<void(const std::exception&)> cbOnException;
};

//iterator function type to iterate over array of functors for submission
typedef std::function<bool(CallableDoubleFunc**, size_t*)> IteratorFunc_t;

//simple array iteration functor:
//on last element returns false and does not change the pointer.
static bool PtrForwardIteration(CallableDoubleFunc** arrayPPtr, size_t* downCounter)
{
  return 0 != *downCounter &&  ++(*arrayPPtr) &&  --(*downCounter);
}

/** A thread pool that shedules tasks represented as functors,
 *  similar to boost::basic_thread_pool.
 *  It never throws except the constructor where only bad_alloc can occur.
 *
 *  The destructor will join the threads,
 *  if you want it to happen earlier or in separate thread -- call joinAll() manually.
 *
 *  To control exceptions raised from execution of the functor
 *  the user must set CallableDoubleFunc.cbOnException callbacks for each task during submission
 *  via the submit(const CallableDoublefunc* array ...) method.
*/
class ThreadsPool : public WebGrep::noncopyable
{
public:

  static bool performSelfTest();

  //can throw std::bad_alloc on when system has got no bytes for spare
  explicit ThreadsPool(uint32_t nthreads = 1);
  virtual ~ThreadsPool() { joinAll(); }
  size_t threadsCount() const;

  bool closed() const;

  //submit 1 task that has no cbOnException callback.
  bool submit(const WebGrep::CallableFunc_t& ftor);

  /** Submit(len) tasks from array of data. A generalized interface
   *  to work with raw pointers or containers by providing iteration functor.
   *  The iteration functor is called after each element access by pointer dereference.
   *  Example for the linked list:
   *
   *  @verbatim
   *  struct LList { CallableDoubleFunc ftor; LList* next;};
   *  LList* list_head = new LList;//fill the linked list with N elements
   *  ThreadsPool thp(10);
   *
   *  auto iterFunc = [](const CallableDoubleFunc** list, size_t* dcount) -> bool
   *  {//iterate over linked list
   *    (void)dcount;//unused
   *    *list = list->next;
   *    return nullptr != *list;
   *  };
   *  thp.submit(list_head, 0, iterFunc, true);
   *
   *  @endverbatim
   *  @param ftorArray: plain array or linked list's head pointer.
   *  @param len: count of elements
   *  @param iterFn: functor to be called after each pointer dereference
*/
  bool submit(CallableDoubleFunc* ftorArray, size_t len,
              IteratorFunc_t iterFn = PtrForwardIteration, bool spray = true);

  void close();  //< close the submission of tasks

  void joinAll();//< sync by joinMutex.
  bool joined(); //< sync by joinMutex.

protected:
  typedef std::shared_ptr<std::thread> ThreadPtr;

  struct ThreadData
  {
    ThreadData() : stopFlag(false)
    {
      workQ.reserve(32);
    }
    std::mutex mu;
    std::condition_variable cond;
    bool stopFlag;
    std::vector<CallableDoubleFunc> workQ;
  };
  typedef std::shared_ptr<ThreadData> ThreadDataPtr;

  virtual void processingLoop(const ThreadDataPtr& td);

  std::vector<ThreadPtr> threadsVec;
  std::vector<ThreadDataPtr> mcVec;
  std::atomic_uint d_current;
  volatile bool d_closed;

  std::mutex joinMutex;
};

}//namespace WebGrep

#endif // THREAD_BOOL_H
