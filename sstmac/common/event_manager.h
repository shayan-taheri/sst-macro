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

#ifndef SSTMAC_COMMON_EVENTMANAGER_H_INCLUDED
#define SSTMAC_COMMON_EVENTMANAGER_H_INCLUDED

#include <sstmac/common/timestamp.h>
#include <sstmac/common/event_location.h>

#include <sstmac/hardware/common/flow_fwd.h>
#include <sstmac/common/event_handler_fwd.h>
#include <sstmac/common/event_scheduler.h>
#include <sstmac/common/sst_event_fwd.h>
#include <sstmac/common/stats/stat_collector_fwd.h>
#include <sprockit/sim_parameters_fwd.h>
#include <sprockit/factory.h>
#include <sprockit/debug.h>
#include <sprockit/allocator.h>

#include <sstmac/hardware/interconnect/interconnect_fwd.h>
#include <sstmac/backends/common/sim_partition_fwd.h>
#include <sstmac/backends/common/parallel_runtime_fwd.h>
#include <sstmac/common/event_scheduler_fwd.h>
#include <sstmac/backends/native/manager_fwd.h>
#include <sstmac/common/sst_event.h>
#include <sstmac/software/threading/threading_interface_fwd.h>

#include <vector>
#include <queue>
#include <cstdint>
#include <cstddef>


DeclareDebugSlot(EventManager);

namespace sstmac {

#if SSTMAC_INTEGRATED_SST_CORE
#else
/**
 * Base type for implementations of an engine that
 * is able to schedule events and advance simulation time
 * in the right order.
 */

class EventManager
{
 public:
  SST_ELI_DECLARE_BASE(EventManager)
  SST_ELI_DECLARE_CTOR(SST::Params&, ParallelRuntime*)

  SST_ELI_REGISTER_DERIVED(
    EventManager,
    EventManager,
    "macro",
    "map",
    SST_ELI_ELEMENT_VERSION(1,0,0),
    "Implements a basic event manager running in serial")

  friend class Component;
  friend class SubComponent;
  friend class EventScheduler;
  friend class native::Manager;

  EventManager(SST::Params& params, ParallelRuntime* rt);

  bool isComplete() {
    return complete_;
  }

  static const GlobalTimestamp* myClock() {
    return global->nowPtr();
  }

  static const GlobalTimestamp no_events_left_time;

  static EventManager* global;

  virtual ~EventManager();

  /**
   * @brief spin_up
   * Create a user-space thread context for the event manager and start it running
   */
  void spinUp(void(*fxn)(void*), void* args);

  /**
   * @brief spinDown
   * Bring down the user-space thread context and go back to original event manager
   */
  void spinDown();

  sw::ThreadContext* cloneThread() const;

  virtual void run();

  void stop();

  Partition* topologyPartition() const;

  ParallelRuntime* runtime() const {
    return rt_;
  }

  void finishStats();

  GlobalTimestamp finalTime() const {
    return final_time_;
  }

  /** 
   * @return The MPI rank of this event manager 
   * */
  int me() const {
    return me_;
  }

  int thread() const {
    return thread_id_;
  }

  void setThread(int thr) {
    thread_id_ = thr;
  }

  int nproc() const {
    return nproc_;
  }

  int nworker() const {
    return nproc_ * nthread_;
  }

  int nthread() const {
    return nthread_;
  }

  void registerStatisticCore(StatisticBase* base, SST::Params& params);

  virtual EventManager* threadManager(int thr) const {
    return const_cast<EventManager*>(this);
  }

  void ipcSchedule(IpcEvent* iev);

  void multithreadSchedule(int slot, int srcThread, ExecutionEvent* ev){
    pending_events_[slot][srcThread].push_back(ev);
  }

  void schedulePendingSerialization(char* buf){
    pending_serialization_.push_back(buf);
  }

  int pendingSlot() const {
    return pendingSlot_;
  }

  void schedule(ExecutionEvent* ev){
    if (ev->time() < now_){
      spkt_abort_printf("Time went backwards on thread %d", thread_id_);
    }
    event_queue_.insert(ev);
  }

  void setInterconnect(hw::Interconnect* ic);

  hw::Interconnect* interconnect() const {
    return interconn_;
  }

  virtual void scheduleStop(GlobalTimestamp until);

  /**
   * @brief run_events
   * @param event_horizon
   * @return Whether no more events or just hit event horizon
   */
  GlobalTimestamp runEvents(GlobalTimestamp event_horizon);

  GlobalTimestamp now() const {
    return now_;
  }

  const GlobalTimestamp* nowPtr() const {
    return &now_;
  }

  void setMinIpcTime(GlobalTimestamp t){
    min_ipc_time_ = std::min(t,min_ipc_time_);
  }

  GlobalTimestamp minEventTime() const {
    return event_queue_.empty()
          ? no_events_left_time
          : (*event_queue_.begin())->time();
  }

 protected:
  void registerPending();

  virtual GlobalTimestamp receiveIncomingEvents(GlobalTimestamp vote) {
    return vote;
  }

#define num_pendingSlots 4
  int pendingSlot_;
  std::vector<std::vector<ExecutionEvent*>> pending_events_[num_pendingSlots];
  std::vector<char*> pending_serialization_;

 protected:
  bool complete_;
  GlobalTimestamp final_time_;
  ParallelRuntime* rt_;
  hw::Interconnect* interconn_;
  sw::ThreadContext* des_context_;
  sw::ThreadContext* main_thread_;
  bool scheduled_;
  bool stopped_;

  int me_;
  int nproc_;

  uint16_t nthread_;
  uint16_t thread_id_;

  Timestamp lookahead_;
  GlobalTimestamp now_;

 private:
#define MAX_EVENT_MGR_THREADS 128
  std::vector<EventScheduler*> pending_registration_[MAX_EVENT_MGR_THREADS];

 protected:
  GlobalTimestamp min_ipc_time_;

  void finalizeStatsOutput();

  void finalizeStatsInit();

  void scheduleIncoming(IpcEvent* iev);

  int serializeSchedule(char* buf);

  struct EventCompare {
    bool operator()(ExecutionEvent* lhs, ExecutionEvent* rhs) const {
      bool neq = lhs->time() != rhs->time();
      if (neq) return lhs->time() < rhs->time();

      if (lhs->linkId() == rhs->linkId()){
        return lhs->seqnum() < rhs->seqnum();
      } else {
        return lhs->linkId() < rhs->linkId();
      }
    }
  };
  using queue_t = std::set<ExecutionEvent*, EventCompare,
                    sprockit::allocator<ExecutionEvent*>> ;
  queue_t event_queue_;

  StatisticOutput* dflt_stat_output_;

  std::map<std::string, StatisticGroup*> stat_groups_;

};

class NullEventManager : public EventManager
{
 public:
  NullEventManager(SST::Params& params, ParallelRuntime* rt) :
    EventManager(params, rt)
  {
  }

  void run() override {}

};
#endif

} // end of namespace sstmac


#endif
