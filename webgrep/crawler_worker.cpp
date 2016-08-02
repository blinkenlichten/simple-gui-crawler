#include "crawler_worker.h"

#include <iostream>
#include "boost/regex.hpp"

namespace WebGrep {

//typedef boost::default_user_allocator_new_delete Al_t;
//typedef boost::detail::spinlock Slock_t;
//---------------------------------------------------------------
//class TaskAllocator : public boost::fast_pool_allocator<char, Al_t, Slock_t, 128 * sizeof(LinkedTask), 0>
//{
//  public:
//};
//---------------------------------------------------------------
void LinkedTask::shallowCopy(const LinkedTask& other)
{
  level = other.level;
  root = other.root;
  parent = other.parent;
  grepExpr = other.grepExpr;
  maxLinkCount = other.maxLinkCount;
  linksCounterPtr = other.linksCounterPtr;
}

//---------------------------------------------------------------
static std::string ExtractHostPortHttp(const std::string& targetUrl)
{
  std::string url(targetUrl);
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

  //func: DOWNLOAD_ONE
  jobfuncs[(int)WorkerAction::DOWNLOAD_ONE] =
      [](LinkedTask* task, Worker* w)
  {
    std::shared_ptr<SimpleWeb::Client>& hclient(w->ctx->httpClient);
    if (nullptr == hclient || std::string::npos == task->targetUrl.find(hclient->host))
      {
        hclient = std::make_shared<SimpleWeb::Client>(w->asio, ExtractHostPortHttp(task->targetUrl));
      }
    std::shared_ptr<SimpleWeb::Response> response = hclient->request("GET", task->targetUrl);
    response->content >> task->pageContent;
  };
  //func: GREP_ONE
  jobfuncs[(int)WorkerAction::GREP_ONE] =
      [this](LinkedTask* task, Worker* w)
  {
    //grep the grepExpr:
    boost::regex_search(task->pageContent, task->matchedText, task->grepExpr);
    //grep the http:// URLs and spawn new nodes:
    boost::regex_search(task->pageContent, task->matchURL, WebGrep::HttpExrp);
    if (task->pageMatchFinishedCb)
      {
        task->pageMatchFinishedCb(task, w);
      }
  };


  //func: DOWNLOAD_GREP_RECURSIVE
  jobfuncs[(int)WorkerAction::DOWNLOAD_AND_GREP_RECURSIVE] =
      [this](LinkedTask* task, Worker* w)
   {
     while(nullptr != task->linksCounterPtr && w->isRunning()
           && task->linksCounterPtr->load() < task->maxLinkCount)
       {
         //download one page:
         jobfuncs[(int)WorkerAction::DOWNLOAD_ONE](task, w);
         if (task->pageContent.empty())
           {
             break;
           }
         //grep page for text & URLs:
         jobfuncs[(int)WorkerAction::GREP_ONE](task, w);

         //create linked list from grepped URLS:
         for(size_t cnt = 0; cnt < task->matchURL.size(); ++cnt)
           {
             task->linksCounterPtr->operator ++();
             //grep it's content for .html links:
             auto child = new LinkedTask();
             child->shallowCopy(*task);
             child->parent = task;
             child->level = 1u + child->parent->level;
             //atomically store the new node
             task->next.store((std::uintptr_t)child);
             jobfuncs[(int)WorkerAction::DOWNLOAD_AND_GREP_RECURSIVE]((LinkedTask*)task->next.load(), w);
           }
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
            jobfuncs[(int)wcmd.command](wcmd.task, this);
            wcmd.taskDisposer(wcmd.task);
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

void Worker::stop()
{
  running = false;
  std::lock_guard<std::mutex> lk(ctx->taskMutex);  (void)lk;
  ctx->cond.notify_all();
  cmdList.clear();
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
