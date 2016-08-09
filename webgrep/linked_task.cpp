#include "linked_task.h"
#include <cassert>
#include <iostream>

namespace WebGrep {

void TraverseFunc(LinkedTask* head, void* additional,
                  void(*func)(LinkedTask*, void*))
{
  if (nullptr == head || nullptr == func)
    return;
  LinkedTask* next = ItemLoadAcquire(head->next);
  for(; nullptr != next; next = ItemLoadAcquire(next->next))
    {
      TraverseFunc(next, additional, func);
    }
  LinkedTask* child = ItemLoadAcquire(head->child);
  if(nullptr != child) {
      TraverseFunc(child, additional, func);
    }
  func(head, additional);
}

// Recursively traverse the list and call functor on each item
void TraverseFunctor(LinkedTask* head, void* additional,
                     std::function<void(LinkedTask*, void* additional/*nullptr*/)> func)
{
  std::cerr << "......\n";
  if (nullptr == head || nullptr == func)
    return;
  LinkedTask* next = ItemLoadAcquire(head->next);
  for(; nullptr != next; next = ItemLoadAcquire(next->next))
    {
      TraverseFunctor(next, additional, func);
    }
  LinkedTask* child = ItemLoadAcquire(head->child);
  if(nullptr != child) {
      TraverseFunctor(child, additional, func);
    }

  func(head, additional);
}

static void DeleteCall(LinkedTask* item, void* data)
{
  if ((void*)item == data)
    return;
  delete item;
}

// Free memory recursively.
void DeleteList(LinkedTask* head)
{
  TraverseFunc(head, head, &DeleteCall);
  delete head;
}
//---------------------------------------------------------------
LinkedTask::LinkedTask() : level(0)
{
  order = 0;
  next.store(0);
  child.store(0);
  root.store(0);
  parent.store(0);
  childNodesCount.store(0);
}



//---------------------------------------------------------------
void LinkedTask::shallowCopy(const LinkedTask& other)
{
  level = other.level;
  root.store(other.root.load());
  parent.store(other.parent.load());
  {
    grepVars.grepExpr = other.grepVars.grepExpr;
  }

  maxLinksCountPtr = other.maxLinksCountPtr;
  linksCounterPtr = other.linksCounterPtr;
}

LinkedTask* LinkedTask::getLastOnLevel()
{
  std::atomic_uintptr_t* freeslot = &next;
  LinkedTask* item = ItemLoadAcquire(*freeslot);
  LinkedTask* last_item = item;
  for(; nullptr != item; item = ItemLoadAcquire(*freeslot))
    {
      freeslot = &(item->next);
      last_item = item;
    }
  if (nullptr == last_item)
    last_item = this;
  return last_item;
}

size_t LinkedTask::spawnNextNodes(size_t nodesCount)
{
  if (0 == nodesCount)
    return 0;

  //go to the end elemet:
  LinkedTask* last_item = getLastOnLevel();

  //spawn items on same level (access by .next)
  size_t c = 0;
  try {

    //put 1 item into last zero holder:
    LinkedTask* item = nullptr;
    for(; c < nodesCount; ++c, last_item = item)
      {
        item = new LinkedTask;
        StoreAcquire(last_item->next, item);
        {
          item->shallowCopy(*this);
          StoreAcquire(item->parent, this);
          item->level = 1u + this->level;
          item->order = childNodesCount.load(std::memory_order_acquire);
          this->childNodesCount.fetch_add(1);
        }
      };
  }catch(std::exception& ex)
  {
    std::cerr << ex.what() << "\n";
    std::cerr << __FUNCTION__ << "\n";
  }
  return c;
}

size_t LinkedTask::spawnChildNodes(size_t nodesCount)
{
  if (0 == nodesCount)
    return 0;

  //backup pointer to exsiting childs:
  LinkedTask* existing_bkup = ItemLoadAcquire(this->child);
  LinkedTask* item = nullptr;
  size_t spawnCnt = 0;
  try {
    //spawn subtree list's head:
    StoreAcquire(child, new LinkedTask);
    item = (LinkedTask*)child.load();
    {
      item->shallowCopy(*this);
      item->parent.store((std::uintptr_t)this);
      item->level = 1u + this->level;
      item->order = childNodesCount.load(std::memory_order_acquire);
      this->childNodesCount.fetch_add(1);
    }
    if (++spawnCnt < nodesCount)
      {
        spawnCnt += item->spawnNextNodes(nodesCount - 1);
      }

  } catch(std::exception& ex)
  {
    std::cerr << __FUNCTION__ << std::endl;
    std::cerr << ex.what() << std::endl;
  }
  //now node is a pointer to last element or zero
  //put previously spawned child nodes to the end of a new list:
  if (nullptr != existing_bkup && nullptr != item)
    {
      StoreAcquire(item->next, existing_bkup);
    }

  return spawnCnt;
}

size_t ForEachOnBranch(LinkedTask* head,
                       std::function<void(LinkedTask*, void*)> functor,
                       bool includingHead, void* additional)
{
  LinkedTask* item = includingHead? head : ItemLoadAcquire(head->next);
  for(size_t cnt = 0; nullptr != item; ++cnt, item = ItemLoadAcquire(item->next))
    {
      functor(item, additional);
    }
  return cnt;
}

size_t LinkedTask::spawnGreppedSubtasks(const std::string& host_and_port)
{
  if (!grepVars.pageIsParsed)
    return 0;

  auto func = [](LinkedTask* node, void*)
  {
    std::string& turl(node->grepVars.targetUrl);
    turl.assign(grepVars.matchURLVector[c].first,
                grepVars.matchURLVector[c].second);
    auto httpPos = turl.find_first_of("http");
    if (std::string::npos != httpPos)
      {
        return;
      }
    //deal with local href links: href=/resource.html or leave if href="http://.."
    //grab http:// or https://
    auto nodeParent = ItemLoadAcquire(node->parent);
    localLink = nodeParent->grepVars.targetUrl.substr(0, 3 + grepVars.targetUrl.find_first_of("://"));
    // append site.com:443
    localLink += host_and_port;
    // append local resource URI
    if ('/' != turl[0])
      localLink += "/";
    localLink += turl;
    turl = localLink;
  };

  //spawn N items (leafs) on current branch
  size_t spawnedListSize = spawnNextNodes(grepVars.matchURLVector.size());

  //for each leaf: configure it with target URL:
  size_t cnt = ForEachOnBranch(this, func, true);
  return std::min(cnt, spawnedListSize);
}

//---------------------------------------------------------------

}//WebGrep
