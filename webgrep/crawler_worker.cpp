#include "crawler_worker.h"

#include <iostream>
#include "boost/regex.hpp"
#include "boost/pool/pool_alloc.hpp"

namespace WebGrep {

typedef boost::default_user_allocator_new_delete Al_t;
typedef boost::detail::spinlock Slock_t;
//---------------------------------------------------------------
class TaskAllocator : public boost::pool_allocator<char, Al_t, Slock_t, 128, 0>
{
  public:
};
//---------------------------------------------------------------
void LinkedTask::shallowCopy(const LinkedTask& other)
{
  level = other.level;
  root = other.root;
  parent = other.parent;
  {
    const GrepVars& og(other.grepVars);
    grepVars.grepExpr = og.grepExpr;
//    grepVars.allocatorPtr = og.allocatorPtr;
  }

  maxLinkCount = other.maxLinkCount;
  linksCounterPtr = other.linksCounterPtr;
  childLevelSpawned = other.childLevelSpawned;
  pageMatchFinishedCb = other.pageMatchFinishedCb;
}

void LinkedTask::killSubtree()
{

}
//---------------------------------------------------------------
static std::string ExtractHostPortHttp(const std::string& targetUrl)
{
  std::string url(targetUrl.data());
  if (std::string::npos != url.find_first_of("http"))
    {
      url = targetUrl.substr(url.find_first_of("://"));
    }
  auto slash_pos = url.find_first_of('/');
  if (std::string::npos != slash_pos)
    {
      url.resize(slash_pos);
    }
  return url;
}

Worker::Worker() : ctx(new WorkerCtx)
{
  running = false;
  cmdList.reserve(128);
  temp.reserve(128);

  //func: DOWNLOAD_ONE
  jobfuncs[(int)WorkerAction::DOWNLOAD_ONE] =
      [this](LinkedTask* task, WorkerPtr w)
  {
    std::shared_ptr<SimpleWeb::Client>& hclient(w->ctx->httpClient);
    std::string& url(task->grepVars.targetUrl);
    if (nullptr == hclient || std::string::npos == url.find(hclient->host.data()))
      {
        hclient = std::make_shared<SimpleWeb::Client>(w->asio, ExtractHostPortHttp(url).data());
      }
    std::shared_ptr<SimpleWeb::Response> response = hclient->request("GET", url.data());
    //copy the page
    response->content >> task->grepVars.pageContent;
  };

  //func: GREP_ONE
  jobfuncs[(int)WorkerAction::GREP_ONE] =
      [this](LinkedTask* task, WorkerPtr w)
  {

    GrepVars& g(task->grepVars);
    if (g.pageContent.empty())
      {
        jobfuncs[(int)WorkerAction::DOWNLOAD_ONE](task, w);
      }
    if (g.pageContent.empty())
      return;

    //grep the grepExpr:
    boost::regex_search(g.pageContent, g.matchedText, g.grepExpr);
    //grep the http:// URLs and spawn new nodes:
    boost::regex_search(g.pageContent, g.matchURL, WebGrep::HttpExrp);
    g.pageIsReady = true;
    if (task->pageMatchFinishedCb)
      {
        task->pageMatchFinishedCb(task, w);
      }
  };


  //func: DOWNLOAD_GREP_RECURSIVE
  jobfuncs[(int)WorkerAction::DOWNLOAD_AND_GREP_RECURSIVE] =
      [this](LinkedTask* task, WorkerPtr w)
   {
     if (task->maxLinkCount <= task->linksCounterPtr->load(std::memory_order_acquire))
       {
         w->ctx->onMaximumLinksCount(task, w);
         return;
       }
     if (nullptr == task->linksCounterPtr || !w->isRunning()
           || task->linksCounterPtr->load(std::memory_order_acquire) < task->maxLinkCount)
       {
         return;
       }
     //download one page:
     jobfuncs[(int)WorkerAction::DOWNLOAD_ONE](task, w);
     GrepVars& g(task->grepVars);
     if (g.pageContent.empty())
       {
         return;
       }
     //grep page for (text and URLs):
     jobfuncs[(int)WorkerAction::GREP_ONE](task, w);
     if (g.matchURL.empty())
       {
         //no URLS then no subtree items.
         return;
       }

     //create next level linked list from grepped URLS:
     {
       auto child = new LinkedTask;
       { //fill fields of 1st child node
         child->shallowCopy(*task);
         child->parent = task;
         child->level = 1u + task->level;
         std::stringstream out(child->grepVars.targetUrl);
         out << g.matchURL[0];
       }
       //put child to task.child
       task->childNodesCount.fetch_add(1, std::memory_order_release);
       task->child.store((std::uintptr_t)child, std::memory_order_release);
       //process next children: (one for each URL)

       //spawn items on same level (access by .next)
       for(unsigned cnt = 1; cnt < g.matchURL.size() - 1;
           --cnt, child = ItemLoadAcquire(child->next))
         {
           auto item = new LinkedTask;
           item->shallowCopy(*child);
           std::stringstream out(item->grepVars.targetUrl);
           out << g.matchURL[cnt];
           item->parent = task;
           task->childNodesCount.fetch_add(1, std::memory_order_release);
           child->next.store((std::uintptr_t)item, std::memory_order_release);
         }
       //emit signal that we've spawned a new level:
       if (task->childLevelSpawned)
         {
           task->childLevelSpawned(ItemLoadAcquire(task->child), w);
         }
     }
     //for each item of subtree: call self recursively
     auto child = ItemLoadAcquire(task->child);
     for(; nullptr != child; child = ItemLoadAcquire(child->next))
     {
       jobfuncs[(int)WorkerAction::DOWNLOAD_AND_GREP_RECURSIVE](child, w);
     }

     for(size_t cnt = 0; cnt < g.matchURL.size(); ++cnt)
       {
         task->linksCounterPtr->operator ++();
         //grep it's content for .html links:
         auto child = new LinkedTask();
         child->shallowCopy(*task);
         child->parent = task;

         //call self:
         jobfuncs[(int)WorkerAction::DOWNLOAD_AND_GREP_RECURSIVE]
             (ItemLoadAcquire(task->next), w);
       }
  };


  jobsLoop = [this](WorkerCtx* wctx)
  {
    while (running)
      {
        std::unique_lock<std::mutex> lk(wctx->taskMutex);  (void)lk;
        wctx->cond.wait(lk);
        while(!cmdList.empty() && running)
          {
            WorkerCommand& wcmd(cmdList.back());
            if (WorkerAction::LOOP_QUIT == wcmd.command)
              {
                running = false;
                break;
              }
            jobfuncs[(int)wcmd.command](wcmd.task, shared_from_this());
            wcmd.taskDisposer(wcmd.task, shared_from_this());
            cmdList.pop_back();
          }
        cmdList.clear();
      }
  };
}

bool Worker::start()
{
  try {
    if (nullptr == asio)
      asio.reset(new boost::asio::io_service);
    if (nullptr == thread)
      {
        thread.reset(new std::thread(jobsLoop, ctx.get()));
      }
    running = true;
  } catch(const std::exception& e)
  {
    std::cerr << e.what();
    return false;
  }
  return true;
}

std::vector<WorkerCommand> Worker::stop()
{
  running = false;
  std::lock_guard<std::mutex> lk(ctx->taskMutex);  (void)lk;
  ctx->cond.notify_all();
  return cmdList;
}

bool Worker::put(WorkerCommand command)
{
  try {
    std::lock_guard<std::mutex> lk(ctx->taskMutex);  (void)lk;
    cmdList.push_back(command);
    ctx->cond.notify_all();
  } catch(std::exception& e)
  {
    std::cerr << e.what();
    return false;
  }
  return true;
}

bool Worker::put(WorkerCommand* commandsArray, unsigned cnt)
{
  try {
    std::lock_guard<std::mutex> lk(ctx->taskMutex);  (void)lk;
    for(unsigned c = 0; c < cnt; ++c)
      {
        cmdList.push_back(commandsArray[c]);
      }
    ctx->cond.notify_all();
  } catch(std::exception& e)
  {
    std::cerr << e.what();
    return false;
  }
  return true;
}
//---------------------------------------------------------------


}//WebGrep
