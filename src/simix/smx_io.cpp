/* Copyright (c) 2007-2018. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#include <xbt/ex.hpp>
#include <xbt/sysdep.h>
#include <xbt/log.h>

#include "simgrid/s4u/Host.hpp"
#include "simgrid/s4u/Storage.hpp"

#include "smx_private.hpp"
#include "src/kernel/activity/IoImpl.hpp"
#include "src/simix/smx_io_private.hpp"
#include "src/surf/HostImpl.hpp"
#include "src/surf/StorageImpl.hpp"
#include "src/surf/surf_interface.hpp"
#include "surf/surf.hpp"

XBT_LOG_NEW_DEFAULT_SUBCATEGORY(simix_io, simix, "Logging specific to SIMIX (io)");

void simcall_HANDLER_storage_read(smx_simcall_t simcall, surf_storage_t st, sg_size_t size)
{
  smx_activity_t synchro = SIMIX_storage_read(st, size);
  synchro->simcalls_.push_back(simcall);
  simcall->issuer->waiting_synchro = synchro;
}

smx_activity_t SIMIX_storage_read(surf_storage_t st, sg_size_t size)
{
  simgrid::kernel::activity::IoImpl* synchro = new simgrid::kernel::activity::IoImpl();
  synchro->surf_action_                      = st->read(size);

  synchro->surf_action_->set_data(synchro);
  XBT_DEBUG("Create io synchro %p", synchro);

  return synchro;
}

void simcall_HANDLER_storage_write(smx_simcall_t simcall, surf_storage_t st, sg_size_t size)
{
  smx_activity_t synchro = SIMIX_storage_write(st, size);
  synchro->simcalls_.push_back(simcall);
  simcall->issuer->waiting_synchro = synchro;
}

smx_activity_t SIMIX_storage_write(surf_storage_t st, sg_size_t size)
{
  simgrid::kernel::activity::IoImpl* synchro = new simgrid::kernel::activity::IoImpl();
  synchro->surf_action_                      = st->write(size);
  synchro->surf_action_->set_data(synchro);
  XBT_DEBUG("Create io synchro %p", synchro);

  return synchro;
}

void SIMIX_io_destroy(smx_activity_t synchro)
{
  simgrid::kernel::activity::IoImplPtr io = boost::static_pointer_cast<simgrid::kernel::activity::IoImpl>(synchro);
  XBT_DEBUG("Destroy synchro %p", synchro.get());
  if (io->surf_action_)
    io->surf_action_->unref();
}

void SIMIX_io_finish(smx_activity_t synchro)
{
  for (smx_simcall_t const& simcall : synchro->simcalls_) {
    switch (synchro->state_) {
      case SIMIX_DONE:
        /* do nothing, synchro done */
        break;
      case SIMIX_FAILED:
        SMX_EXCEPTION(simcall->issuer, io_error, 0, "IO failed");
        break;
      case SIMIX_CANCELED:
        SMX_EXCEPTION(simcall->issuer, cancel_error, 0, "Canceled");
        break;
      default:
        xbt_die("Internal error in SIMIX_io_finish: unexpected synchro state %d", static_cast<int>(synchro->state_));
    }

    if (simcall->issuer->host->is_off()) {
      simcall->issuer->context->iwannadie = 1;
    }

    simcall->issuer->waiting_synchro = nullptr;
    SIMIX_simcall_answer(simcall);
  }

  /* We no longer need it */
  SIMIX_io_destroy(synchro);
}
