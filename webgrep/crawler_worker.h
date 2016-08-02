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

/** LinkedTask object is not thread-safe.*/
class LinkedTask : public boost::noncopyable
{
public:
  LinkedTask() : level(0), root(nullptr), parent(nullptr)
  {
    maxLinkCount = 256;
    next.store(0);
  }
  //shallow copy without {.next, .targetUrl, .pageContent}
  void shallowCopy(const LinkedTask& other);

  unsigned level;
  LinkedTask* root;
  LinkedTask* parent;
  std::atomic_uintptr_t next;//cast to (LinkedTask*)

  std::string targetUrl;
  boost::regex grepExpr;  //< regexp to be matched
  std::string pageContent;//< html content

  //contains(string::const_iterator) matched results of regexp in .grepExptr from .pageContent
  boost::smatch matchedText;

  //contains matched URLs in .pageContent
  boost::smatch matchURL;

  unsigned maxLinkCount;
  std::atomic_uint* linksCounterPtr;//not null

  std::function<void(LinkedTask*, Worker* w)> pageMatchFinishedCb;

};
//---------------------------------------------------------------
struct WorkerCommand
{
  WorkerCommand(): command(WorkerAction::NONE_LAST), task(nullptr)
  {
    taskDisposer = [](LinkedTask*){/*empty ftor*/};
  }
  WorkerAction command;

  //must be disposed manually by taskDisposer(LinkedTask*);
  LinkedTask* task;
  std::function<void(LinkedTask*)> taskDisposer;
};
//---------------------------------------------------------------
struct WorkerCtx
{
  WorkerCtx() { }
  std::condition_variable cond;
  std::mutex taskMutex;
  std::shared_ptr<SimpleWeb::Client> httpClient;
  std::shared_ptr<TaskAllocator> taskAllocator;
};
//---------------------------------------------------------------
class Worker
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
  typedef std::function<void(LinkedTask*, Worker*)> JobFunc_t;
  std::array<JobFunc_t, (int)WorkerAction::NONE_LAST> jobfuncs;
  std::function<void(WorkerCtx*)> jobsLoop;

  std::shared_ptr<boost::asio::io_service> asio;
  std::shared_ptr<WorkerCtx> ctx;
protected:
  bool running;
  std::shared_ptr<std::thread> thread;
  std::vector<WorkerCommand> cmdList;
};
//---------------------------------------------------------------

}//WebGrep

#endif // CRAWLER_WORKER_H
