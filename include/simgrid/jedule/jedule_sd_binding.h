/* Copyright (c) 2010-2018. The SimGrid Team. All rights reserved.          */

/* This program is free software; you can redistribute it and/or modify it
 * under the terms of the license (GNU LGPL) which comes with this package. */

#ifndef JEDULE_SD_BINDING_H_
#define JEDULE_SD_BINDING_H_

#include <simgrid/config.h>
#include <simgrid/simdag.h>

#if SIMGRID_HAVE_JEDULE
SG_BEGIN_DECL()
XBT_PUBLIC void jedule_log_sd_event(SD_task_t task);
XBT_PUBLIC void jedule_sd_init(void);
XBT_PUBLIC void jedule_sd_exit(void);
XBT_PUBLIC void jedule_sd_dump(const char* filename);
SG_END_DECL()
#endif

#endif /* JEDULE_SD_BINDING_H_ */
