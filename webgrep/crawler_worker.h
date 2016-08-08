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
/** Downloads the page content and stores it in (GrepVars)task->grepVars
 *  variable, (volatile bool)task->grepVars.pageIsReady will be set to TRUE
 *  on successfull download; the page content will be stored in task->grepVars.pageContent
*/
bool FuncDownloadOne(LinkedTask* task, WorkerCtxPtr w);

/** Calls FuncDownloadOne(task, w) if FALSE == (volatile bool)task->grepVars.pageIsReady,
 *  then if the download is successfull it'll grep the http:// and href= links from the page,
 *  the results are stored in a container of type (vector<pair<string::const_iterator,string::const_iterator>>)
 *  at variable (task->grepVars.matchURLVector and task->grepVars.matchTextVector)
 *  where each pair of const iterators points to the begin and the end of matched strings
 *  in task->grepVars.pageContent, the iterators are valid until grepVars.pageContent exists.
*/
bool FuncGrepOne(LinkedTask* task, WorkerCtxPtr w);

/** Call FuncDownloadOne(task,w) multiple times: once for each new http:// URL
 *  in a page's content. It won't use resursion, but will utilize appropriate
 *  callbacks to put new tasks as functors in a multithreaded work queue.
*/
bool FuncDownloadGrepRecursive(LinkedTask* task, WorkerCtxPtr w);
//---------------------------------------------------------------

struct WorkerCtx
{
  WorkerCtx() {
    hrefGrepExpr = boost::regex("<\\s*A\\s+[^>]*href\\s*=\\s*\"([^\"]*)\"",
                                boost::regex::normal | boost::regbase::icase);
    hrefGrepExpr2 = boost::regex("<\\s*A\\s+[^>]*href\\s*=\\s*\"/([_A-Za-z0-9]*)/\"",
                                boost::regex::normal | boost::regbase::icase);
    urlGrepExpr = boost::regex(" (http:|https:)//[a-zA-Z0-9./?=_-]*");//boost::regex("(http|https)://[a-zA-Z0-9./?=_-]*");
    running = true;
  }

  //used to notify pending tasks on the current one:
  std::condition_variable cond;
  std::mutex taskMutex;

  WebGrep::Client httpClient;
  std::string hostPort;
  boost::regex urlGrepExpr, hrefGrepExpr, hrefGrepExpr2;

  std::function<void(LinkedTask*, WorkerCtxPtr, const std::string& )> onException;

  //the tasks can schedule subtasks
  typedef std::function<void()> CallableFunc_t;
  std::function<void(CallableFunc_t)> sheduleTask;

  //callbacks, all three are set externally:

  //when max. links count reached.
  std::function<void(LinkedTask*, WorkerCtxPtr)> onMaximumLinksCount;

  /** Invoked on each page parsed.*/
  std::function<void(LinkedTask*, std::shared_ptr<WorkerCtx> w)> pageMatchFinishedCb;

  /** Invoked when a new level of child nodes has spawned, */
  std::function<void(LinkedTask*,std::shared_ptr<WorkerCtx> w)> childLevelSpawned;


  volatile bool running;//< used for subtask spawn pausing

};
//---------------------------------------------------------------

}//WebGrep

#endif // CRAWLER_WORKER_H
