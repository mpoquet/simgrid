/* Copyright (c) 2004-2018. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include "simgrid/kernel/resource/Model.hpp"
#include "src/kernel/lmm/maxmin.hpp"

XBT_LOG_EXTERNAL_DEFAULT_CATEGORY(resource);

namespace simgrid {
namespace kernel {
namespace resource {

Model::Model(Model::UpdateAlgo algo) : update_algorithm_(algo) {}

Model::~Model()
{
  delete inited_action_set_;
  delete started_action_set_;
  delete failed_action_set_;
  delete finished_action_set_;
  delete ignored_action_set_;
  delete maxmin_system_;
}

Action::ModifiedSet* Model::get_modified_set() const
{
  return maxmin_system_->modified_set_;
}

double Model::next_occuring_event(double now)
{
  // FIXME: set the good function once and for all
  if (update_algorithm_ == Model::UpdateAlgo::LAZY)
    return next_occuring_event_lazy(now);
  else if (update_algorithm_ == Model::UpdateAlgo::FULL)
    return next_occuring_event_full(now);
  else
    xbt_die("Invalid cpu update mechanism!");
}

double Model::next_occuring_event_lazy(double now)
{
  XBT_DEBUG("Before share resources, the size of modified actions set is %zu", maxmin_system_->modified_set_->size());
  maxmin_system_->lmm_solve();
  XBT_DEBUG("After share resources, The size of modified actions set is %zu", maxmin_system_->modified_set_->size());

  while (not maxmin_system_->modified_set_->empty()) {
    Action* action = &(maxmin_system_->modified_set_->front());
    maxmin_system_->modified_set_->pop_front();
    bool max_duration_flag = false;

    if (action->get_state_set() != started_action_set_)
      continue;

    /* bogus priority, skip it */
    if (action->get_priority() <= 0 || action->get_type() == ActionHeap::Type::latency)
      continue;

    action->update_remains_lazy(now);

    double min   = -1;
    double share = action->get_variable()->get_value();

    if (share > 0) {
      double time_to_completion;
      if (action->get_remains() > 0) {
        time_to_completion = action->get_remains_no_update() / share;
      } else {
        time_to_completion = 0.0;
      }
      min = now + time_to_completion; // when the task will complete if nothing changes
    }

    if ((action->get_max_duration() > NO_MAX_DURATION) &&
        (min <= -1 || action->get_start_time() + action->get_max_duration() < min)) {
      // when the task will complete anyway because of the deadline if any
      min          = action->get_start_time() + action->get_max_duration();
      max_duration_flag = true;
    }

    XBT_DEBUG("Action(%p) corresponds to variable %d", action, action->get_variable()->id_int);

    XBT_DEBUG("Action(%p) Start %f. May finish at %f (got a share of %f). Max_duration %f", action,
              action->get_start_time(), min, share, action->get_max_duration());

    if (min > -1) {
      action_heap_.update(action, min, max_duration_flag ? ActionHeap::Type::max_duration : ActionHeap::Type::normal);
      XBT_DEBUG("Insert at heap action(%p) min %f now %f", action, min, now);
    } else
      DIE_IMPOSSIBLE;
  }

  // hereafter must have already the min value for this resource model
  if (not action_heap_.empty()) {
    double min = action_heap_.top_date() - now;
    XBT_DEBUG("minimum with the HEAP %f", min);
    return min;
  } else {
    XBT_DEBUG("The HEAP is empty, thus returning -1");
    return -1;
  }
}

double Model::next_occuring_event_full(double /*now*/)
{
  maxmin_system_->solve();

  double min = -1;

  for (Action& action : *get_started_action_set()) {
    double value = action.get_variable()->get_value();
    if (value > 0) {
      if (action.get_remains() > 0)
        value = action.get_remains_no_update() / value;
      else
        value = 0.0;
      if (min < 0 || value < min) {
        min = value;
        XBT_DEBUG("Updating min (value) with %p: %f", &action, min);
      }
    }
    if ((action.get_max_duration() >= 0) && (min < 0 || action.get_max_duration() < min)) {
      min = action.get_max_duration();
      XBT_DEBUG("Updating min (duration) with %p: %f", &action, min);
    }
  }
  XBT_DEBUG("min value : %f", min);

  return min;
}

void Model::update_actions_state(double now, double delta)
{
  if (update_algorithm_ == Model::UpdateAlgo::FULL)
    update_actions_state_full(now, delta);
  else if (update_algorithm_ == Model::UpdateAlgo::LAZY)
    update_actions_state_lazy(now, delta);
  else
    xbt_die("Invalid cpu update mechanism!");
}

void Model::update_actions_state_lazy(double /*now*/, double /*delta*/)
{
  THROW_UNIMPLEMENTED;
}

void Model::update_actions_state_full(double /*now*/, double /*delta*/)
{
  THROW_UNIMPLEMENTED;
}

} // namespace surf
} // namespace simgrid
} // namespace simgrid
