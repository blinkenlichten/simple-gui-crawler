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


namespace WebGrep {

class TaskAllocator;

enum class WorkerAction
{
  LOOP_QUIT,
  DOWNLOAD_ONE,
  DOWNLOAD_AND_GREP_RECURSIVE, GREP_RECURSIVE, NONE_LAST
};

class LinkedTask : public boost::noncopyable
{
public:
  LinkedTask() : level(0), root(nullptr), parent(nullptr), next(nullptr)
  {
    maxLinkCount = 256;
  }
  //shallow copy without targetUrl, pageContent
  void shallowCopy(const LinkedTask& other);

  unsigned level;
  LinkedTask* root;
  LinkedTask* parent;
  LinkedTask* next;

  std::string targetUrl;
  std::string grepString;
  std::string pageContent;

  std::shared_ptr<TaskAllocator> taskAllocator; //must not be null

  unsigned maxLinkCount;
  std::atomic_uint* linksCounterPtr;//not null
};
//---------------------------------------------------------------
struct WorkerCommand
{
  WorkerCommand(): command(WorkerAction::NONE_LAST), task(nullptr) { }
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
  void stop();  //< send command to stop and join the thread
  bool isRunning() const {return running;}

  virtual bool put(WorkerCommand command);

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
