#ifndef LINKEDTASK_H
#define LINKEDTASK_H
#include <string>
#include "boost/regex.hpp"
#include "boost/noncopyable.hpp"
#include <atomic>

namespace WebGrep {

class Worker;
//---------------------------------------------------------------

struct GrepVars
{
  GrepVars() : pageIsReady(false) { }
  std::string targetUrl;
  boost::regex grepExpr;  //< regexp to be matched
  std::string pageContent;//< html content

  //contains(string::const_iterator) matched results of regexp in .grepExptr from .pageContent
  boost::smatch matchedText;

  //contains matched URLs in .pageContent
  boost::smatch matchURL;

  //must be set to true when it's safe to access .pageContent from other threads:
  bool pageIsReady;
};
//---------------------------------------------------------------

/** LinkedTask object is not thread-safe.*/
class LinkedTask : public boost::noncopyable
{
public:
  LinkedTask() : level(0), root(nullptr), parent(nullptr)
  {
    maxLinkCount = 256;
    next.store(0, std::memory_order_release);
    child.store(0, std::memory_order_release);
    childNodesCount.store(0, std::memory_order_release);
  }
  //shallow copy without {.next, .targetUrl, .pageContent}
  void shallowCopy(const LinkedTask& other);

  //level of this node
  unsigned level;
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
  std::atomic_uint* linksCounterPtr;//not null

  std::function<void(LinkedTask*, std::shared_ptr<Worker> w)> pageMatchFinishedCb;
  /** Invoked when a new level of child nodes has spawned,
*/
  std::function<void(LinkedTask*,std::shared_ptr<Worker> w)> childLevelSpawned;

};
//---------------------------------------------------------------
static inline LinkedTask* ItemLoadAcquire(std::atomic_uintptr_t& value)
{
  return (LinkedTask*)value.load(std::memory_order_acquire);
}

}//WebGrep

#endif // LINKEDTASK_H
