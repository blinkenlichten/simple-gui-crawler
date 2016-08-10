#ifndef CRAWLER_PRIVATE_H
#define CRAWLER_PRIVATE_H
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


class CrawlerPV : public std::enable_shared_from_this<CrawlerPV>
{
public:
  CrawlerPV()
  {
    maxLinksCount = std::make_shared<std::atomic_uint>();
    currentLinksCount = std::make_shared<std::atomic_uint>();

    maxLinksCount->store(4096);
    currentLinksCount->store(0);
    workersPool = std::make_shared<boost::executors::basic_thread_pool>(4);

    selfTest();
  }

  virtual ~CrawlerPV()
  {
    try {
      clear();
    } catch(...)
    { }
  }

  bool selfTest() const;
  void stopThreads();

  //spawn a context and bind to shared_from_this()
  WorkerCtx makeWorkerContext();

  //this method serves as sheduling method for objects LonelyTask (Task pointers basically)
  void sheduleTask(const WebGrep::LonelyTask& task);
  //this method serves as sheduling method for functors
  void sheduleFunctor(CallableFunc_t func);
  void stop() {  }


  void clear()
  {
    stopThreads();
    taskRoot.reset();
    currentLinksCount->store(0);
  }
  std::function<void(const std::string& what)> onException;
  std::function<void(std::shared_ptr<LinkedTask> rootNode, LinkedTask* node)>  onPageScanned;

  //main task (for first html page) all other are subtasks
  std::shared_ptr<LinkedTask> taskRoot;

  std::shared_ptr<boost::executors::basic_thread_pool> workersPool;

  //these shared by all tasks spawned by the object CrawlerPV:
  std::shared_ptr<std::atomic_uint> maxLinksCount, currentLinksCount;

  //---- these variables track for abandoned tasks that are to be re-issued:
  // these provide sync. access to lonelyVector, lonelyFunctorsVector
  typedef boost::detail::spinlock LonelyLock_t;
  boost::detail::spinlock slockLonely, slockLonelyFunctors;

  //these are keeping abandoned tasks that should be resheduled
  std::vector<WebGrep::LonelyTask> lonelyVector;
  std::vector<WebGrep::CallableFunc_t> lonelyFunctorsVector;
};

}//namespace WebGrep

#endif // CRAWLER_PRIVATE_H
