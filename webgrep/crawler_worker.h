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
#include "boost/asio/ssl/context.hpp"
#include "linked_task.h"


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

typedef std::shared_ptr<Worker> WorkerPtr;
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
struct WorkerCtx;
//---------------------------------------------------------------

class Worker : public std::enable_shared_from_this<Worker>
{
public:
  Worker();
  virtual ~Worker();
  bool start(); //< start attached thread

  /** send command to stop and join the thread.
   * @return unfinished tasks vector. */
  std::vector<WorkerCommand> stop();

  bool isRunning() const {return running;}

  virtual bool put(WorkerCommand command);
  virtual bool put(WorkerCommand* commandsArray, unsigned cnt);

  //when max. links sount reached. Set externally.
  std::function<void(LinkedTask*, WorkerPtr)> onMaximumLinksCount;

  /** The advantage over virtual method is that
   *  these can be swapped like a hot potato*/
  typedef std::function<bool(LinkedTask*, WorkerPtr)> JobFunc_t;
  std::array<JobFunc_t, (int)WorkerAction::NONE_LAST> jobfuncs;
  std::function<void(const WorkerPtr&)> jobsLoop;

  SimpleWeb::ClientConfig httpConfig;

  std::shared_ptr<WorkerCtx> ctx;
  std::vector<WorkerCommand> cmdList;
protected:
  volatile bool running;
  std::unique_ptr<std::thread> thread;
  std::string temp;
};
//---------------------------------------------------------------

}//WebGrep

#endif // CRAWLER_WORKER_H
