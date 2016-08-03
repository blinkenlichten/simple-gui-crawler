#ifndef CRAWLER_WORKER_H
#define CRAWLER_WORKER_H

#include <memory>
#include <functional>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <boost/lockfree/queue.hpp>
#include <boost/noncopyable.hpp>
#include "client_http.hpp"
#include <atomic>
#include <array>
#include "boost/regex.hpp"


namespace WebGrep {

class TaskAllocator;

enum class WorkerAction
{
  LOOP_QUIT,
  /*downloads one http page via GET*/
  DOWNLOAD_ONE,
  /*spawns child nodes, downloads pages and greps again*/
  DOWNLOAD_AND_GREP_RECURSIVE,
  /*spaws child list of tasks with urls*/
  GREP_ONE,
  NONE_LAST
};

static boost::regex HttpExrp( "^(?:http://)?([^/]+)(?:/?.*/?)/(.*)$" );

class Worker;
typedef std::shared_ptr<Worker> WorkerPtr;
//---------------------------------------------------------------
typedef boost::default_user_allocator_new_delete Al_t;
typedef boost::detail::spinlock Slock_t;
typedef boost::pool_allocator<char, Al_t, Slock_t, 128, 0> CharAllocator ;
//---------------------------------------------------------------
struct GrepVars
{
  GrepVars() : pageIsReady(false) { }
  typedef std::basic_string<char, std::char_traits<char>, CharAllocator> poolstring;
  std::string targetUrl;
  boost::regex grepExpr;  //< regexp to be matched
  std::string pageContent;//< html content

  //contains(string::const_iterator) matched results of regexp in .grepExptr from .pageContent
  boost::smatch matchedText;

  //contains matched URLs in .pageContent
  boost::smatch matchURL;

  //must be set to true when it's safe to access .pageContent from other threads:
  bool pageIsReady;

//  std::shared_ptr<CharAllocator> allocatorPtr;
};



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
  void killSubtree();

  //level of this node
  unsigned level;
  LinkedTask* root;
  LinkedTask* parent;

  //counds next/child nodes: load() is acquire
  std::atomic_int childNodesCount;

  //store: memory_order_release
  //load : memory_order_acquire

  /** (LinkedTask*)next points to same level item,
   *  (LinkedTask*)child points to next level items(subtree).
*/
  std::atomic_uintptr_t next, child;//< cast to (LinkedTask*)

  GrepVars grepVars;

  unsigned maxLinkCount;
  std::atomic_uint* linksCounterPtr;//not null

  std::function<void(LinkedTask*, WorkerPtr w)> pageMatchFinishedCb;
  /** Invoked when a new level of child nodes has spawned,
*/
  std::function<void(LinkedTask*,WorkerPtr w)> childLevelSpawned;

};

static LinkedTask* ItemLoadAcquire(std::atomic_uintptr_t& value)
{
  return (LinkedTask*)value.load(std::memory_order_acquire);
}
//---------------------------------------------------------------
struct WorkerCommand
{
  WorkerCommand(): command(WorkerAction::NONE_LAST), task(nullptr)
  {
    taskDisposer = [](LinkedTask*, WorkerPtr){/*empty ftor*/};
  }
  WorkerAction command;

  //must be disposed manually by taskDisposer(LinkedTask*);
  LinkedTask* task;
  std::function<void(LinkedTask*, WorkerPtr)> taskDisposer;
};
//---------------------------------------------------------------
struct WorkerCtx
{
  WorkerCtx() {
   allocatorPtr = std::make_shared<CharAllocator>();
   onMaximumLinksCount = [](LinkedTask*, WorkerPtr){ };
  }
  std::condition_variable cond;
  std::mutex taskMutex;
  std::shared_ptr<SimpleWeb::Client> httpClient;
  std::shared_ptr<CharAllocator> allocatorPtr;

  //when max. links sount reached. Set externally.
  std::function<void(LinkedTask*, WorkerPtr)> onMaximumLinksCount;
};
//---------------------------------------------------------------


class Worker : public std::enable_shared_from_this<Worker>
{
public:
  Worker();
  bool start(); //< start attached thread

  /** send command to stop and join the thread.
   * @return unfinished tasks vector. */
  std::vector<WorkerCommand> stop();

  bool isRunning() const {return running;}

  virtual bool put(WorkerCommand command);
  virtual bool put(WorkerCommand* commandsArray, unsigned cnt);

  /** The advantage over virtual method is that
   *  these can be swapped like a hot potato*/
  typedef std::function<void(LinkedTask*, WorkerPtr)> JobFunc_t;
  std::array<JobFunc_t, (int)WorkerAction::NONE_LAST> jobfuncs;
  std::function<void(WorkerCtx*)> jobsLoop;

  std::shared_ptr<boost::asio::io_service> asio;
  std::shared_ptr<WorkerCtx> ctx;
protected:
  bool running;
  std::shared_ptr<std::thread> thread;
  std::vector<WorkerCommand> cmdList;
  std::string temp;
};
//---------------------------------------------------------------

}//WebGrep

#endif // CRAWLER_WORKER_H
