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

static boost::regex HttpExrp( "^(?:http://)?([^/]+)(?:/?.*/?)/(.*)$" );

//---------------------------------------------------------------
struct WorkerCtx;
typedef std::shared_ptr<WorkerCtx> WorkerCtxPtr;
//---------------------------------------------------------------
bool FuncDownloadOne(LinkedTask* task, WorkerCtxPtr w);
bool FuncGrepOne(LinkedTask* task, WorkerCtxPtr w);
bool FuncDownloadGrepRecursive(LinkedTask* task, WorkerCtxPtr w);
//---------------------------------------------------------------
struct WorkerCtx
{
  WorkerCtx() {running = true;}
  std::condition_variable cond;
  std::mutex taskMutex;
  std::shared_ptr<SimpleWeb::Client> httpClient;

  SimpleWeb::ClientConfig httpConfig;

  std::string temp;

  //when max. links sount reached. Set externally.
  std::function<void(LinkedTask*, WorkerCtxPtr)> onMaximumLinksCount;

  //the tasks can schedule subtasks
  typedef std::function<void()> CallableFunc_t;
  std::function<void(CallableFunc_t)> sheduleTask;

  volatile bool running;//< used for pausing

};
//---------------------------------------------------------------

}//WebGrep

#endif // CRAWLER_WORKER_H
