#include "linked_task.h"
#include <cassert>
#include <iostream>

namespace WebGrep {

void TraverseFunc(LinkedTask* head, void* additional,
                  void(*func)(LinkedTask*, void*))
{
  if (nullptr == head || nullptr == func) return;
  LinkedTask* next = ItemLoadAcquire(head->next);
  LinkedTask* child = ItemLoadAcquire(head->child);
  func(head, additional);
  TraverseFunc(next, additional, func);
  TraverseFunc(child, additional, func);
}

// Recursively traverse the list and call functor on each item
void TraverseFunctor(LinkedTask* head, void* additional,
                     std::function<void(LinkedTask*, void* additional/*nullptr*/)> func)
{
  if (nullptr == head || nullptr == func) return;
  LinkedTask* next = ItemLoadAcquire(head->next);
  LinkedTask* child = ItemLoadAcquire(head->child);
  func(head, additional);
  TraverseFunctor(next, additional, func);
  TraverseFunctor(child, additional, func);
}

static void DeleteCall(LinkedTask* item, void* data)
{
  (void)data;
  delete item;
}

// Free memory recursively.
void DeleteList(LinkedTask* head)
{
  TraverseFunc(head, head, &DeleteCall);
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

LinkedTask* LinkedTask::spawnChildNode(LinkedTask*& expelledChild)
{
  expelledChild = ItemLoadAcquire(child);
  try {
    auto item = new LinkedTask;
    StoreAcquire(child, item);
    item->shallowCopy(*this);
    item->parent.store((std::uintptr_t)this);
    item->level = 1u + this->level;
    item->order = childNodesCount.load(std::memory_order_acquire);
    this->childNodesCount.fetch_add(1);
    return item;
  } catch(std::exception& ex)
  {
    std::cerr << __FUNCTION__ << std::endl;
    std::cerr << ex.what() << std::endl;
  }
  return nullptr;
}

size_t ForEachOnBranch(LinkedTask* head,
                       std::function<void(LinkedTask*, void*)> functor,
                       bool includingHead, void* additional)
{
  LinkedTask* item = includingHead? head : (nullptr != head? ItemLoadAcquire(head->next) : nullptr );
  size_t cnt = 0;
  for(; nullptr != item; ++cnt, item = ItemLoadAcquire(item->next))
    {
      functor(item, additional);
    }
  return cnt;
}

size_t LinkedTask::spawnGreppedSubtasks(const std::string& host_and_port, const GrepVars& targetVariables)
{
  if (!targetVariables.pageIsParsed)
    return 0;

  size_t cposition = 0;
  auto func = [this, &cposition, &host_and_port, &targetVariables](LinkedTask* node, void*)
  {
    std::string& turl(node->grepVars.targetUrl);
    turl.assign(targetVariables.matchURLVector[cposition].first,
                targetVariables.matchURLVector[cposition].second);
    auto httpPos = turl.find_first_of("http");
    if (std::string::npos != httpPos && '/' != turl[0])
      {//neither of http:// or "/resource" link types
        return;
      }
    //deal with local href links: href=/resource.html or leave if href="http://.."
    //grab http:// or https://
    auto nodeParent = ItemLoadAcquire(node->parent);
    if (nullptr == nodeParent) {
        //case it's the root node:
        nodeParent = this;
      }

    std::string localLink = targetVariables.scheme.data(); //"http" or "https"
    localLink += "://";
    // append site.com:443
    localLink += host_and_port;
    // append local resource URI
    localLink += turl;
    turl = localLink;
    ++cposition;
  };

  //spawn N items (leafs) on current branch
  size_t spawnedListSize = spawnNextNodes(targetVariables.matchURLVector.size());
  //for each leaf: configure it with target URL:
  size_t cnt = ForEachOnBranch(this, func, false/*skip (this) element*/);
  linksCounterPtr->fetch_add(cnt);
  return std::min(cnt, spawnedListSize);
}

//---------------------------------------------------------------

}//WebGrep
