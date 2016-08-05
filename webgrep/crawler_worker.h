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
  WorkerCtx() {
    hrefGrepExpr = boost::regex("<\\s*A\\s+[^>]*href\\s*=\\s*\"([^\"]*)\"",
                                boost::regex::normal | boost::regbase::icase);
    urlGrepExpr = boost::regex(" (http://|https://)[a-zA-Z0-9./?=_-]*");//boost::regex("(http|https)://[a-zA-Z0-9./?=_-]*");
    running = true;
  }

  //used to notify pending tasks on the current one:
  std::condition_variable cond;
  std::mutex taskMutex;

  WebGrep::Client httpClient;
  std::string hostPort;
  boost::regex urlGrepExpr, hrefGrepExpr;

  boost::smatch matchURL;
  std::map<std::string, GrepVars::CIteratorPair> matches;

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
