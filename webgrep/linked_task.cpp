#include "linked_task.h"
#include <cassert>
#include <iostream>

namespace WebGrep {

void TraverseFunc(LinkedTask* head, void* additional,
                  void(*func)(LinkedTask*, void*))
{
  if (nullptr == head || nullptr == func)
    return;
  for(LinkedTask* next = ItemLoadAcquire(head->next);
      nullptr != next; next = ItemLoadAcquire(head->next))
    {
      func(next, additional);
    }
  func(ItemLoadAcquire(head->child), additional);
}

// Recursively traverse the list and call functor on each item
void TraverseFunctor(LinkedTask* head, void* additional,
                     std::function<void(LinkedTask*, void* additional/*nullptr*/)> func)
{
  if (nullptr == head || nullptr == func)
    return;
  for(LinkedTask* next = ItemLoadAcquire(head->next);
      nullptr != next; next = ItemLoadAcquire(head->next))
    {
      func(next, additional);
    }
  func(ItemLoadAcquire(head->child), additional);
}

static void DeleteCall(LinkedTask* item, void* data)
{
  (void)data;
  delete item;
}

// Free memory recursively.
void DeleteList(LinkedTask* head)
{
  TraverseFunc(head, nullptr, &DeleteCall);
  delete head;
}


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

  maxLinksCountPtr = other.maxLinksCountPtr;
  linksCounterPtr = other.linksCounterPtr;
  childLevelSpawned = other.childLevelSpawned;
  pageMatchFinishedCb = other.pageMatchFinishedCb;
}

size_t LinkedTask::spawnChildNodes(size_t nodesCount)
{
  if (0 == nodesCount)
    return 0;

  size_t spawnCnt = 0;
  //backup pointer to exsiting childs:
  LinkedTask* existing_bkup = ItemLoadAcquire(this->child);
  LinkedTask* item = nullptr;
  try {
    //spawn subtree list's head:
    this->child.store((std::uintptr_t)(new LinkedTask), std::memory_order_acquire);
    ++spawnCnt;
    item = (LinkedTask*)child.load();
    {
      item->shallowCopy(*this);
      item->parent = this;
      item->level = 1u + this->level;
      item->order = this->childNodesCount.load(std::memory_order_acquire);
      this->childNodesCount.fetch_add(1);
    }

    //spawn items on same level (access by .next)
    do
      {
        item->next.store((std::uintptr_t)(new LinkedTask), std::memory_order_acquire);
        item = (LinkedTask*)item->next.load(std::memory_order_relaxed);
        {
          item->shallowCopy(*this);
          item->parent = this;
          item->level = 1u + this->level;
          item->order = this->childNodesCount.load(std::memory_order_acquire);
          this->childNodesCount.fetch_add(1);
        }
      } while(++spawnCnt < nodesCount);
  } catch(std::exception& ex)
  {
    std::cerr << __FUNCTION__ << std::endl;
    std::cerr << ex.what() << std::endl;
  }
  //now node is a pointer to last element or zero
  //put previously spawned child nodes to the end of a new list:
  if (nullptr != existing_bkup && nullptr != item)
    {
      item->next.store((std::uintptr_t)existing_bkup, std::memory_order_acquire);
    }

  return spawnCnt;
}

size_t LinkedTask::spawnGreppedSubtasks(const std::string& host_and_port)
{
  size_t spawnedCnt = spawnChildNodes(grepVars.matchURLVector.size());
  LinkedTask* node = ItemLoadAcquire(child);
  std::string localLink;
  for(size_t c = 0; c < spawnedCnt && nullptr != node;
      ++c, node = ItemLoadAcquire(node->next))
    {
      std::string& turl(node->grepVars.targetUrl);
      turl.assign(grepVars.matchURLVector[c].first,
                  grepVars.matchURLVector[c].second);
      auto httpPos = turl.find_first_of("http");
      if (std::string::npos == httpPos)
        {
          //deal with local href links: href=resource.html or leave if href="http://.."
          auto quoteLast = turl.find_last_of('\"');
          auto quotePos = turl.find_first_of('\"');
          //grab http:// or https://
          localLink = node->parent->grepVars.targetUrl.substr(0, 3 + grepVars.targetUrl.find_first_of("://"));
          // append site.com:443
          localLink += host_and_port;
          // append local resource URI
          localLink += "/";
          localLink += turl.substr(quotePos, quoteLast);
          turl = localLink;
        }
    }
  return spawnedCnt;
}

//---------------------------------------------------------------

}//WebGrep
