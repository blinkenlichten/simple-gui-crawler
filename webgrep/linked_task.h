#ifndef LINKEDTASK_H
#define LINKEDTASK_H
#include <string>
#include "boost/regex.hpp"
#include "boost/noncopyable.hpp"
#include <atomic>

namespace WebGrep {

class WorkerCtx;
//---------------------------------------------------------------

struct GrepVars
{
  GrepVars() : responseCode(0), pageIsReady(false), pageIsParsed(false)
  { }
  std::string targetUrl;
  boost::regex grepExpr;  //< regexp to be matched
  int responseCode;
  std::string pageContent;//< html content

  //contains(string::const_iterator) matched results of regexp in .grepExptr from .pageContent
  boost::smatch matchedText;

  //contains matched URLs in .pageContent
  typedef std::pair<std::string::const_iterator,std::string::const_iterator> CIteratorPair;
  std::vector<CIteratorPair> matchURLVector;

  //must be set to true when it's safe to access .pageContent from other threads:
  volatile bool pageIsReady;
  volatile bool pageIsParsed;
};
//---------------------------------------------------------------

/** LinkedTask object is not thread-safe.*/
class LinkedTask : public boost::noncopyable
{
public:
  LinkedTask() : level(0), root(nullptr), parent(nullptr)
  {
    order = 0;
    maxLinksCountPtr = nullptr;
    linksCounterPtr = nullptr;
    next.store(0, std::memory_order_release);
    child.store(0, std::memory_order_release);
    childNodesCount.store(0, std::memory_order_release);
  }
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
  LinkedTask* root;
  LinkedTask* parent;

  //counds next/child nodes: load() is acquire
  std::atomic_int childNodesCount;

  /** (LinkedTask*)next points to same level item,
   *  (LinkedTask*)child points to next level items(subtree).
   * load : memory_order_acquire
   * store: memory_order_release */
  std::atomic_uintptr_t next, child;

  GrepVars grepVars;

  unsigned maxLinkCount;
  // you must have guaranteed that these are set & will live longer than any LinkedTask object
  std::atomic_uint* linksCounterPtr, *maxLinksCountPtr;

  std::function<void(LinkedTask*, std::shared_ptr<WorkerCtx> w)> pageMatchFinishedCb;
  /** Invoked when a new level of child nodes has spawned,
*/
  std::function<void(LinkedTask*,std::shared_ptr<WorkerCtx> w)> childLevelSpawned;

};
//---------------------------------------------------------------
static inline LinkedTask* ItemLoadAcquire(std::atomic_uintptr_t& value)
{
  return (LinkedTask*)value.load(std::memory_order_acquire);
}

}//WebGrep

#endif // LINKEDTASK_H
