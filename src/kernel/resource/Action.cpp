/* Copyright (c) 2004-2018. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include "simgrid/kernel/resource/Action.hpp"
#include "simgrid/kernel/resource/Model.hpp"
#include "src/kernel/lmm/maxmin.hpp"
#include "src/surf/surf_interface.hpp"

XBT_LOG_NEW_CATEGORY(kernel, "Logging specific to the internals of SimGrid");
XBT_LOG_NEW_DEFAULT_SUBCATEGORY(resource, kernel, "Logging specific to the resources");

namespace simgrid {
namespace kernel {
namespace resource {

Action::Action(simgrid::kernel::resource::Model* model, double cost, bool failed) : Action(model, cost, failed, nullptr)
{
}

Action::Action(simgrid::kernel::resource::Model* model, double cost, bool failed, kernel::lmm::Variable* var)
    : remains_(cost), start_time_(surf_get_clock()), cost_(cost), model_(model), variable_(var)
{
  if (failed)
    state_set_ = get_model()->get_failed_action_set();
  else
    state_set_ = get_model()->get_running_action_set();

  state_set_->push_back(*this);
}

Action::~Action()
{
  if (state_set_hook_.is_linked())
    simgrid::xbt::intrusive_erase(*state_set_, *this);
  if (get_variable())
    get_model()->get_maxmin_system()->variable_free(get_variable());
  if (get_model()->getUpdateMechanism() == Model::UpdateAlgo::Lazy) {
    /* remove from heap */
    heapRemove();
    if (modified_set_hook_.is_linked())
      simgrid::xbt::intrusive_erase(*get_model()->get_modified_set(), *this);
  }

  xbt_free(category_);
}

void Action::finish(Action::State state)
{
  finish_time_ = surf_get_clock();
  set_state(state);
  set_remains(0);
}

Action::State Action::get_state() const
{
  if (state_set_ == model_->get_ready_action_set())
    return Action::State::ready;
  if (state_set_ == model_->get_running_action_set())
    return Action::State::running;
  if (state_set_ == model_->get_failed_action_set())
    return Action::State::failed;
  if (state_set_ == model_->get_done_action_set())
    return Action::State::done;
  return Action::State::not_in_the_system;
}

void Action::set_state(Action::State state)
{
  simgrid::xbt::intrusive_erase(*state_set_, *this);
  switch (state) {
    case Action::State::ready:
      state_set_ = model_->get_ready_action_set();
      break;
    case Action::State::running:
      state_set_ = model_->get_running_action_set();
      break;
    case Action::State::failed:
      state_set_ = model_->get_failed_action_set();
      break;
    case Action::State::done:
      state_set_ = model_->get_done_action_set();
      break;
    default:
      state_set_ = nullptr;
      break;
  }
  if (state_set_)
    state_set_->push_back(*this);
}

double Action::get_bound() const
{
  return variable_ ? variable_->get_bound() : 0;
}

void Action::set_bound(double bound)
{
  XBT_IN("(%p,%g)", this, bound);
  if (variable_)
    get_model()->get_maxmin_system()->update_variable_bound(variable_, bound);

  if (get_model()->getUpdateMechanism() == Model::UpdateAlgo::Lazy && get_last_update() != surf_get_clock())
    heapRemove();
  XBT_OUT();
}

void Action::set_category(const char* category)
{
  category_ = xbt_strdup(category);
}

void Action::ref()
{
  refcount_++;
}

void Action::set_max_duration(double duration)
{
  max_duration_ = duration;
  if (get_model()->getUpdateMechanism() == Model::UpdateAlgo::Lazy) // remove action from the heap
    heapRemove();
}

void Action::set_priority(double weight)
{
  XBT_IN("(%p,%g)", this, weight);
  sharing_priority_ = weight;
  get_model()->get_maxmin_system()->update_variable_weight(get_variable(), weight);

  if (get_model()->getUpdateMechanism() == Model::UpdateAlgo::Lazy)
    heapRemove();
  XBT_OUT();
}

void Action::cancel()
{
  set_state(Action::State::failed);
  if (get_model()->getUpdateMechanism() == Model::UpdateAlgo::Lazy) {
    if (modified_set_hook_.is_linked())
      simgrid::xbt::intrusive_erase(*get_model()->get_modified_set(), *this);
    heapRemove();
  }
}

int Action::unref()
{
  refcount_--;
  if (not refcount_) {
    delete this;
    return 1;
  }
  return 0;
}

void Action::suspend()
{
  XBT_IN("(%p)", this);
  if (suspended_ != SuspendStates::sleeping) {
    get_model()->get_maxmin_system()->update_variable_weight(get_variable(), 0.0);
    if (get_model()->getUpdateMechanism() == Model::UpdateAlgo::Lazy) {
      heapRemove();
      if (state_set_ == get_model()->get_running_action_set() && sharing_priority_ > 0) {
        // If we have a lazy model, we need to update the remaining value accordingly
        update_remains_lazy(surf_get_clock());
      }
    }
    suspended_ = SuspendStates::suspended;
  }
  XBT_OUT();
}

void Action::resume()
{
  XBT_IN("(%p)", this);
  if (suspended_ != SuspendStates::sleeping) {
    get_model()->get_maxmin_system()->update_variable_weight(get_variable(), get_priority());
    suspended_ = SuspendStates::not_suspended;
    if (get_model()->getUpdateMechanism() == Model::UpdateAlgo::Lazy)
      heapRemove();
  }
  XBT_OUT();
}

bool Action::is_suspended()
{
  return suspended_ == SuspendStates::suspended;
}
/* insert action on heap using a given key and a hat (heap_action_type)
 * a hat can be of three types for communications:
 *
 * NORMAL = this is a normal heap entry stating the date to finish transmitting
 * LATENCY = this is a heap entry to warn us when the latency is payed
 * MAX_DURATION =this is a heap entry to warn us when the max_duration limit is reached
 */
void Action::heapInsert(double key, Action::Type hat)
{
  type_       = hat;
  heap_hook_  = get_model()->getActionHeap().emplace(std::make_pair(key, this));
}

void Action::heapRemove()
{
  type_ = Action::Type::NOTSET;
  if (heap_hook_) {
    get_model()->getActionHeap().erase(*heap_hook_);
    clearHeapHandle();
  }
}

void Action::heapUpdate(double key, Action::Type hat)
{
  type_ = hat;
  if (heap_hook_) {
    get_model()->getActionHeap().update(*heap_hook_, std::make_pair(key, this));
  } else {
    heap_hook_ = get_model()->getActionHeap().emplace(std::make_pair(key, this));
  }
}

double Action::get_remains()
{
  XBT_IN("(%p)", this);
  /* update remains before return it */
  if (get_model()->getUpdateMechanism() == Model::UpdateAlgo::Lazy) /* update remains before return it */
    update_remains_lazy(surf_get_clock());
  XBT_OUT();
  return remains_;
}

void Action::update_max_duration(double delta)
{
  double_update(&max_duration_, delta, sg_surf_precision);
}
void Action::update_remains(double delta)
{
  double_update(&remains_, delta, sg_maxmin_precision * sg_surf_precision);
}

void Action::set_last_update()
{
  last_update_ = surf_get_clock();
}

} // namespace surf
} // namespace simgrid
} // namespace simgrid