#include "crawler_worker.h"

#include <iostream>

namespace WebGrep {

typedef boost::default_user_allocator_new_delete Al_t;
typedef boost::detail::spinlock Slock_t;
//---------------------------------------------------------------
class TaskAllocator : public boost::fast_pool_allocator<char, Al_t, Slock_t, 128 * sizeof(LinkedTask), 0>
{
  public:

};
//---------------------------------------------------------------
void LinkedTask::shallowCopy(const LinkedTask& other)
{
  level = other.level;
  root = other.root;
  parent = other.parent;
  grepString = other.grepString;
  taskAllocator = other.taskAllocator;
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
  ctx->taskAllocator.reset(new TaskAllocator);
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
  //func: DOWNLOAD_RECURSIVE
  jobfuncs[(int)WorkerAction::DOWNLOAD_AND_GREP_RECURSIVE] =
      [this](LinkedTask* task, Worker* w)
   {
     while(w->isRunning() && task->linksCounterPtr->load() < task->maxLinkCount)
       {
         //download one page:
         jobfuncs[(int)WorkerAction::DOWNLOAD_ONE](task, w);
         if (task->pageContent.empty())
           {
             break;
           }


         task->linksCounterPtr->operator ++();
         {
           //grep it's content for .html links:
           auto child = new LinkedTask();
           child->shallowCopy(*task);
           child->level++;
           child->parent = task;
           task->next = child;
           task = task->next;
           jobfuncs[(int)WorkerAction::DOWNLOAD_AND_GREP_RECURSIVE](task, w);
         }
       }
   };


  jobsLoop = [this](WorkerCtx* wctx)
  {
    while (running)
      {
        std::unique_lock<std::mutex> lk(wctx->taskMutex);  (void)lk;
        wctx->cond.wait(lk);
        for(auto iter = cmdList.begin(); iter != cmdList.end(); ++iter)
          {
            WorkerCommand& wcmd(cmdList.front());
            if (WorkerAction::LOOP_QUIT == wcmd.command)
              {
                running = false;
                break;
              }
            jobfuncs[(int)wcmd.command](wcmd.task, this);
          }
        cmdList.clear();
      }
  };
}

bool Worker::start()
{
  try {
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
//---------------------------------------------------------------


}//WebGrep
