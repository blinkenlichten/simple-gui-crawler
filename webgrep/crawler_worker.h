#ifndef CRAWLER_WORKER_H
#define CRAWLER_WORKER_H

#include <memory>
#include <functional>
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
typedef WorkerCtx* WorkerCtxPtr;
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
 *  in a page's content. It won't use recursion, but will utilize appropriate
 *  callbacks to put new tasks as functors in a multithreaded work queue.
 *
 *  What it does with the list (LinkedTask*) task?
 *  It spawns .child item assigning it (level + 1), it'll be head of a new list
 *  that will contain parse results for the task's page and other subtasks,
 *  appearance of each subtask is caused by finding a URL that we can grep
 *  until counter's limit is reached.
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

  WebGrep::Client httpClient;
  std::string hostPort;
  boost::regex urlGrepExpr, hrefGrepExpr, hrefGrepExpr2;

  std::function<void(LinkedTask*, WorkerCtxPtr, const std::string& )> onException;

  //the tasks can schedule subtasks
  typedef std::function<void()> CallableFunc_t;

  /** Call this one to shedule a new task:*/
  std::function<void(CallableFunc_t)> sheduleTask;

  //---- callbacks, all three are set by the working functions -------

  /** when max. links count reached.*/
  std::function<void(LinkedTask*)> onMaximumLinksCount;

  /** Invoked on each page parsed.*/
  std::function<void(LinkedTask*)> pageMatchFinishedCb;

  /** Invoked when a new level of child nodes has spawned, */
  std::function<void(LinkedTask*)> childLevelSpawned;


  volatile bool running;//< used for subtask spawn pausing

};
//---------------------------------------------------------------

}//WebGrep

#endif // CRAWLER_WORKER_H
