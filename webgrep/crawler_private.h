#ifndef CRAWLER_PRIVATE_H
#define CRAWLER_PRIVATE_H
#include <iostream>
#include "crawler_worker.h"
#include <atomic>
#include <vector>
#include <thread>
#include "thread_pool.h"
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

    selfTest();
    workersPool = std::make_shared<WebGrep::ThreadsPool>(1);
  }

  virtual ~CrawlerPV()
  {
    try {
      clear();
    } catch(...)
    { }
  }

  bool selfTest() const;

  //spawn a context and bind to shared_from_this()
  WorkerCtx makeWorkerContext();

  //this method serves as sheduling method for objects LonelyTask (Task pointers basically)
  //@return FALSE on exception (like bad alloc etc.)
  bool sheduleTask(const WebGrep::LonelyTask& task);

  //this method serves as sheduling method for functors
  //@return FALSE on exception (like bad alloc etc.)
  bool sheduleFunctor(CallableFunc_t func);

  /** It will suspend current tasks by hiding them into a "pocket",
   *  from where it can be pulled out and processed later. */
  void stop();

  /** Starts the root processing task that will spawn subtasks
    this->taskRoot will be first element is the root of the tree.
    The tree will have structure like following:

    @param neuRootTask: must be constructed with
    taskRoot.grepVars.targetUrl and optionally taskRoot.grepVars.grepRegex values set.
    @param threadsNumber: quantity of working threads to use.
    @param forceRebuild: force to parse all URLs even if it is done already.

    @verbatim

    [root(first URL)]
          |
          | {grep page and get array of matched URLs into root.grepVars.matchURLVector}
          | {spawn 1 child with URL1}
          | {spawn a chain of new nodes attached to child with URL1 using
          |  size_t LinkedTask::spawnGreppedSubtasks(const std::string& host_and_port, const GrepVars&, size_t);}
          |
          +--> [child(URL1)]-->[child->next (URL2)]-->[next->next (URL3)]
                    |                   |
                    |                   +-[child2->child{URL2.1}]-----[URL{2.K}]
                    |
                    | {recursive call to bool FuncDownloadGrepRecursive(LinkedTask* task, WorkerCtx& w)
                    |  for each node spaws new subtasks that create new child nodes.
                    |
                [child->child{URL1.1}]---[URL{1.2}]--... [URL{1.M}]

    @endverbatim

  */
  void start(std::shared_ptr<LinkedTask> neuRootTask,
             unsigned threadsNumber = 4, bool forceRebuild = false);


  //stop all tasks ASAP and clear otu everything
  void clear();

  std::function<void(const std::string& what)> onException;
  std::function<void(std::shared_ptr<LinkedTask> rootNode, LinkedTask* node)>  onPageScanned;
  //-----------------------------------------------------------------------------
  /** main task (for first html page) all other are subtasks
  must be allocated explicitly before start().
  */
  std::shared_ptr<LinkedTask> taskRoot;
  //-----------------------------------------------------------------------------

  /** Multithreaded task exec. entity.*/
  std::shared_ptr<WebGrep::ThreadsPool> workersPool;

  //these shared by all tasks spawned by the object CrawlerPV:
  std::shared_ptr<std::atomic_uint> maxLinksCount, currentLinksCount;

  //---- these variables track for abandoned tasks that are to be re-issued:
  // these provide sync. access to lonelyVector, lonelyFunctorsVector
  typedef std::mutex LonelyLock_t;
  LonelyLock_t slockLonely, slockLonelyFunctors;

  //these are keeping abandoned tasks that should be resheduled
  std::vector<WebGrep::LonelyTask> lonelyVector;
  std::vector<WebGrep::CallableFunc_t> lonelyFunctorsVector;
};

}//namespace WebGrep

#endif // CRAWLER_PRIVATE_H
