/**
Copyright 2009-2018 National Technology and Engineering Solutions of Sandia, 
LLC (NTESS).  Under the terms of Contract DE-NA-0003525, the U.S.  Government 
retains certain rights in this software.

Sandia National Laboratories is a multimission laboratory managed and operated
by National Technology and Engineering Solutions of Sandia, LLC., a wholly 
owned subsidiary of Honeywell International, Inc., for the U.S. Department of 
Energy's National Nuclear Security Administration under contract DE-NA0003525.

Copyright (c) 2009-2018, NTESS

All rights reserved.

Redistribution and use in source and binary forms, with or without modification, 
are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.

    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.

    * Neither the name of the copyright holder nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

Questions? Contact sst-macro-help@sandia.gov
*/

#include <sstmac/common/event_scheduler.h>
#include <sstmac/common/sstmac_config.h>
#include <sstmac/common/event_handler.h>
#include <sstmac/common/event_manager.h>
#include <sstmac/common/sst_event.h>
#include <sstmac/common/sstmac_env.h>
#include <sstmac/hardware/node/node.h>
#include <sstmac/common/ipc_event.h>
#include <sstmac/common/handler_event_queue_entry.h>
#include <sprockit/sim_parameters.h>
#include <sprockit/util.h>
#include <unistd.h>

#if SSTMAC_INTEGRATED_SST_CORE
#include <sstmac/sst_core/connectable_wrapper.h>
#endif

#define test_schedule(x) \
  if (dynamic_cast<link_wrapper*>(x)) abort()

namespace sstmac {

EventLink::~EventLink()
{
}

#if SSTMAC_INTEGRATED_SST_CORE
SST::TimeConverter* EventScheduler::time_converter_ = nullptr;
#else
uint32_t
EventLink::allocateLinkId()
{
  auto ret =  linkIdCounter_++;
  return ret;
}

void
EventScheduler::sendExecutionEvent(GlobalTimestamp arrival, ExecutionEvent *ev)
{
  ev->setTime(arrival);
  ev->setSeqnum(seqnum_++);
  ev->setLink(selfLinkId_);
  mgr_->schedule(ev);
}

SST::Params&
EventScheduler::getEmptyParams()
{
  static SST::Params params{};
  return params;
}

void
EventScheduler::registerStatisticCore(StatisticBase *base, SST::Params& params)
{
  mgr_->registerStatisticCore(base, params);
}

void
EventScheduler::setManager()
{
  mgr_ = EventManager::global;
  now_ = mgr_->nowPtr();
}

Timestamp EventLink::minRemoteLatency_;
Timestamp EventLink::minThreadLatency_;
uint32_t EventLink::linkIdCounter_{0};
#endif

void
Component::init(unsigned int phase)
{
#if SSTMAC_INTEGRATED_SST_CORE
  SSTIntegratedComponent::init(phase);
#endif
}

void
Component::setup()
{
#if SSTMAC_INTEGRATED_SST_CORE
  SSTIntegratedComponent::setup();
#endif
}

void
SubComponent::init(unsigned int phase)
{
}

void
SubComponent::setup()
{
#if SSTMAC_INTEGRATED_SST_CORE
  SST::SubComponent::setup();
#endif
}

#if SSTMAC_INTEGRATED_SST_CORE
#else
void
LocalLink::send(Timestamp delay, Event *ev)
{
  GlobalTimestamp arrival = mgr_->now() + delay + latency_;
  ExecutionEvent* qev = new HandlerExecutionEvent(ev, handler_);
  qev->setSeqnum(seqnum_++);
  qev->setTime(arrival);
  qev->setLink(linkId_);
  mgr_->schedule(qev);
}

void
IpcLink::send(Timestamp delay, Event *ev)
{
  GlobalTimestamp arrival = mgr_->now() + delay + latency_;
  mgr_->setMinIpcTime(arrival);
  IpcEvent iev;
  iev.src = srcId_;
  iev.dst = dstId_;
  iev.seqnum = seqnum_++;
  iev.ev = ev;
  iev.t = arrival;
  iev.rank = rank_;
  iev.link = linkId_;
  iev.credit = is_credit_;
  iev.port = port_;
  mgr_->ipcSchedule(&iev);
  //this guy is gone
  delete ev;
}

void
MultithreadLink::send(Timestamp delay, Event* ev)
{
  ExecutionEvent* qev = new HandlerExecutionEvent(ev, handler_);
  GlobalTimestamp arrival = mgr_->now() + delay + latency_;
  mgr_->setMinIpcTime(arrival);
  qev->setTime(arrival);
  qev->setSeqnum(seqnum_++);
  qev->setLink(linkId_);
  dst_mgr_->multithreadSchedule(mgr_->pendingSlot(), mgr_->thread(), qev);
}
#endif

} // end of namespace sstmac
