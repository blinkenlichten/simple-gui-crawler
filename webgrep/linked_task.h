#ifndef LINKEDTASK_H
#define LINKEDTASK_H
#include <string>
#include "boost/regex.hpp"
#include "boost/noncopyable.hpp"
#include <atomic>

#include <mutex>
#include <iostream>

namespace WebGrep {

class WorkerCtx;
//---------------------------------------------------------------

class PageLock_t
{
public:
  std::string logName;

  bool try_lock() noexcept
  {
    return mu.try_lock();
  }

  void lock() noexcept
  {
    if(!logName.empty())
      std::cerr << " >> try lock " << logName << std::endl;
    mu.lock();

    if(!logName.empty())
      std::cerr << " >> locked OK" << logName << std::endl;
  }
  void unlock() noexcept
  {
    if(!logName.empty())
      std::cerr << " << unlock " << logName << std::endl;
    mu.unlock();
  }
  std::mutex mu;
};


struct GrepVars
{
  GrepVars() : responseCode(0), pageIsReady(false), pageIsParsed(false)
  { }

  std::string targetUrl;
  boost::regex grepExpr;  //< regexp to be matched
  int responseCode;       //< last HTTP GET response code

  PageLock_t pageLock;    //< lock this to safely read this->pageContent.
  std::string pageContent;//< html content

  //contains matched URLs in .pageContent
  typedef std::string::const_iterator COT_t;
  typedef std::pair<COT_t,COT_t> CIteratorPair;

  /** After (TRUE == pageIsParsed) matchURLVector will contain const_iterator
   *  that point to a matched string in this->pageContent;
   *  similarly matchTextVector will get iterators pointing to this->pageContent
   *  where text search conditions has met.
 */
  std::vector<CIteratorPair> matchURLVector, matchTextVector;

  //must be set to true when it's safe to access .pageContent from other threads:
  volatile bool pageIsReady;
  volatile bool pageIsParsed;
};
//---------------------------------------------------------------
class LinkedTask;

// Recursively traverse the list and call a function each item
void TraverseFunc(LinkedTask* head, void* additional,
                  void(*func)(LinkedTask*, void*));

// Recursively traverse the list and call functor on each item
void TraverseFunctor(LinkedTask* head, void* additional,
                     std::function<void(LinkedTask*, void* additional)>);

// Free memory recursively. NOT THREAD SAFE! Must be syncronized.
void DeleteList(LinkedTask* head);
//---------------------------------------------------------------


/** LinkedTask : a tree list that is using atomic pointers
 *  for child nodes (to be able to read without locking).
 *  Once the node is constructed -- it is okay to read tree pointers concurrently,
 *  the variable (GrapVars grepVars) must be carefully accessed on callbacks
 *  when (volatile bool)grepVars.pageIsParse has been set, for example.
 */
class LinkedTask : public boost::noncopyable
{
public:
  LinkedTask();

  //shallow copy without {.next, .targetUrl, .pageContent}
  void shallowCopy(const LinkedTask& other);

  /** Create subtree (level + 1) at (LinkedTask)child.load() pointer.
   * If subtree was already there, the old nodes will be pushed back
   * on the same level.
   *
   * @param nodesCount : greater than 0
   * @return quantity of nodes spawned;
 */
  size_t spawnChildNodes(size_t nodesCount);

  /** Scan grepVars.matchURLVector[] and create next level subtree(subtasks).
   * @return quantity of subtasks spawned. */
  size_t spawnGreppedSubtasks(const std::string& host_and_port);

  //level of this node
  unsigned level, order;
  //counds next/child nodes: load() is acquire
  std::atomic_int childNodesCount;

  /** (LinkedTask*)next points to same level item,
   *  (LinkedTask*)child points to next level items(subtree).
   * load : memory_order_acquire
   * store: memory_order_release */
  std::atomic_uintptr_t next, child, root, parent;

  GrepVars grepVars;

  unsigned maxLinkCount;
  // you must have guaranteed that these are set & will live longer than any LinkedTask object
  std::atomic_uint* linksCounterPtr, *maxLinksCountPtr;

};
//---------------------------------------------------------------
static inline LinkedTask* ItemLoadAcquire(std::atomic_uintptr_t& value)
{
  return (LinkedTask*)value.load(std::memory_order_acquire);
}

}//WebGrep

#endif // LINKEDTASK_H
