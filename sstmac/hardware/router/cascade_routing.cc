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

#include <sstmac/hardware/router/router.h>
#include <sstmac/hardware/router/multipath_routing.h>
#include <sstmac/hardware/switch/network_switch.h>
#include <sstmac/hardware/topology/cascade.h>
#include <sstmac/hardware/interconnect/interconnect.h>
#include <sstmac/common/event_manager.h>
#include <sprockit/util.h>
#include <sprockit/sim_parameters.h>
#include <cmath>

#define ftree_rter_debug(...) \
  rter_debug("fat tree: %s", sprockit::printf(__VA_ARGS__).c_str())

namespace sstmac {
namespace hw {

class cascade_minimal_router : public router {
  struct header : public packet::header {
     char num_hops : 3;
     char num_group_hops : 2;
  };
 public:
  FactoryRegister("cascade_minimal",
              router, cascade_minimal_router,
              "router implementing minimal routing for cascade")

  cascade_minimal_router(sprockit::sim_parameters* params, topology *top,
                         network_switch *netsw)
    : router(params, top, netsw)
  {
    cascade_ = safe_cast(cascade, top);
    my_x_ = cascade_->computeX(my_addr_);
    my_y_ = cascade_->computeY(my_addr_);
    my_g_ = cascade_->computeG(my_addr_);
  }

  std::string to_string() const override {
    return "cascade minimal router";
  }

  int num_vc() const override {
    return 2;
  }

  void route(packet *pkt) override {
    uint16_t dir;
    switch_id ej_addr = cascade_->node_to_ejection_switch(pkt->toaddr(), dir);
    if (ej_addr == my_addr_){
      pkt->current_path().outport() = dir;
      pkt->current_path().vc = 0;
      return;
    }

    packet::path& path = pkt->current_path();
    cascade_->minimal_route_to_switch(this, my_addr_, ej_addr, path);
    auto hdr = pkt->get_header<header>();
    path.vc = hdr->num_group_hops;
    if (cascade_->is_global_port(path.outport())){
      ++hdr->num_group_hops;
    }
    ++hdr->num_hops;
  }

 protected:
  int my_x_;
  int my_y_;
  int my_g_;
  cascade* cascade_;
};

/**
class cascade_valiant_router : public cascade_minimal_router {
 public:
  static const char initial_stage = 0;
  static const char valiant_stage = 1;
  static const char final_stage = 2;

  struct header : public cascade_minimal_router::header {
    uint8_t stage_number : 3;
  };

  FactoryRegister("cascade_valiant",
              router, cascade_valiant_router,
              "router implementing valint routing for dragonfly")

  cascade_valiant_router(sprockit::sim_parameters* params, topology *top,
                           network_switch *netsw)
    : cascade_minimal_router(params, top, netsw)
  {
    group_gateways_.resize(cascade_->numG());
    gateway_rotater_.resize(cascade_->numG());

    std::vector<topology::connection> connected;
    for (int x=0; x < cascade_->numX(); ++x){
      for (int y=0; y < cascade_->numY(); ++y){
        switch_id sid = cascade_->get_uid(x,y,my_g_);
        cascade_->connected_outports(sid, connected);
        for (topology::connection& conn : connected){
          int dst_g = cascade_->computeG(conn.dst);
          if (dst_g != my_g_){
            group_gateways_[dst_g].emplace_back(conn.src_outport, sid);
          }
        }
      }
    }

    for (int i=0; i < gateway_rotater_.size(); ++i){
      if (i != my_g_){
        if (group_gateways_[i].empty()){
          spkt_abort_printf("Group %d has no direct valiant connections to group %d",
                            my_g_, i);
        }
        gateway_rotater_[i] = my_addr_ % group_gateways_[i].size();
      }
    }

  }

  std::string to_string() const override {
    return "dragonfly valiant";
  }

  int num_vc() const override {
    return 6;
  }

  void check_valiant_inter_group(packet* pkt, int dst_g)
  {
    uint32_t seed = netsw_->now().ticks();
    uint32_t attempt = 0;
    int new_g = dst_g;
    while (new_g == my_g_ || new_g == dst_g){
      new_g = random_number(dfly_->g(), attempt++, seed);
    }
    auto hdr = pkt->get_header<header>();
    auto val_dest = group_gateways_[new_g][gateway_rotater_[new_g]];
    pkt->current_path().set_outport(val_dest.first);
    pkt->set_dest_switch(val_dest.second);
    gateway_rotater_[new_g] = (gateway_rotater_[new_g] + 1) % group_gateways_[new_g].size();
    hdr->stage_number = valiant_stage;
  }

  void check_valiant_group(packet* pkt, int dst_x, int dst_y){
    uint32_t seed = netsw_->now().ticks();
    uint32_t attempt = 0;
    int new_g = dst_g;
    while (new_g == my_g_ || new_g == dst_g){
      new_g = random_number(cascade_->numG(), attempt++, seed);
    }
    auto hdr = pkt->get_header<header>();
    auto val_dest = group_gateways_[new_g][gateway_rotater_[new_g]];
    pkt->current_path().set_outport(val_dest.first);
    pkt->set_dest_switch(val_dest.second);
    gateway_rotater_[new_g] = (gateway_rotater_[new_g] + 1) % group_gateways_[new_g].size();
    hdr->stage_number = valiant_stage;
  }

  void check_valiant_xy(packet* pkt, int dst_x, int dst_y){
    uint32_t seed = netsw_->now().ticks();
    uint32_t attempt = 0;
    int new_x = dst_x;
    while (new_a == my_x_ || new_x == dst_x){
      new_x = random_number(cascade_->numX(), attempt++, seed);
    }

    attempt = 0;
    int new_y = dst_y;
    while (new_y == my_y_ || new_y == dst_y){
      new_y = random_number(cascade_->numY(), attempt++, seed);
    }

    auto hdr = pkt->get_header<header>();
    pkt->current_path().set_outport(cascade_->x_port(new_x));
    pkt->set_dest_switch(cascade_->get_uid(new_x_,new_y,my_g_));
    hdr->stage_number = valiant_stage;
  }

  void check_valiant_y(packet* pkt, int dst_y){
    uint32_t seed = netsw_->now().ticks();
    uint32_t attempt = 0;
    int new_y = dst_y;
    while (new_y == my_y_ || new_y == dst_y){
      new_y = random_number(cascade_->numY(), attempt++, seed);
    }

    auto hdr = pkt->get_header<header>();
    pkt->current_path().set_outport(cascade_->y_port(new_y));
    pkt->set_dest_switch(cascade_->get_uid(my_x_,new_y,my_g_));
    hdr->stage_number = valiant_stage;
  }

  void check_valiant_x(packet* pkt, int dst_x){
    uint32_t seed = netsw_->now().ticks();
    uint32_t attempt = 0;
    int new_x = dst_x;
    while (new_a == my_x_ || new_x == dst_x){
      new_x = random_number(cascade_->numX(), attempt++, seed);
    }

    auto hdr = pkt->get_header<header>();
    pkt->current_path().set_outport(cascade_->x_port(new_x));
    pkt->set_dest_switch(cascade_->get_uid(new_x,my_y_,my_g_));
    hdr->stage_number = valiant_stage;
  }

  void route(packet *pkt) override {
    uint16_t dir;
    switch_id ej_addr = top_->netlink_to_ejection_switch(pkt->toaddr(), dir);
    if (ej_addr == my_addr_){
      pkt->current_path().outport() = dir;
      pkt->current_path().vc = 0;
      return;
    }

    auto hdr = pkt->get_header<header>();
    switch(hdr->stage_number){
      case initial_stage: {
        int dst_x = cascade_->computeX(ej_addr);
        int dst_y = cascade_->computeY(ej_addr);
        int dst_g = cascade_->computeG(ej_addr);
        if (dst_g == my_g_){
          if (dst_x == my_x_){
            check_valiant_y(pkt, dst_y);
          } else if (dst_y == my_y_){
            check_valiant_x(pkt, dst_x);
          } else {
            check_valiant_xy(pkt, dst_x, dst_y);
          }
        } else {
          check_valiant_inter_group(pkt, dst_g);
        }
        hdr->stage_number = valiant_stage;
        break;
      }
      case valiant_stage: {
        if (my_addr_ == pkt->dest_switch()){
          hdr->stage_number = final_stage;
        } else {
          route_to_switch(pkt, pkt->dest_switch());
          break;
        }
      }
      case final_stage: {
        route_to_switch(pkt, ej_addr);
        pkt->set_dest_switch(ej_addr);
        break;
      }
      break;
    }
    pkt->current_path().vc = hdr->num_hops;
    ++hdr->num_hops;
  }

 protected:
  std::vector<int> gateway_rotater_; //for non-minimal
  std::vector<std::vector<std::pair<int,int>>> group_gateways_;
};


class cascade_ugal_router : public dragonfly_valiant_router {

 public:
  static const char minimal_only_stage = final_stage + 1;
  FactoryRegister("dragonfly_ugal",
              router, dragonfly_ugal_router,
              "router implementing UGAL routing for dragonfly")

  dragonfly_ugal_router(sprockit::sim_parameters* params, topology *top,
                                 network_switch *netsw)
    : dragonfly_valiant_router(params, top, netsw)
  {
    val_threshold_ = params->get_optional_int_param("val_threshold", 0);
  }

  std::string to_string() const override {
    return "dragonfly ugal router";
  }

  int num_vc() const override {
    return 6;
  }

  void check_ugal_inter_group(packet* pkt, int dst_a, int dst_g, char minimal_stage)
  {
    uint32_t seed = netsw_->now().ticks();
    uint32_t attempt = 0;
    int new_g = dst_g;
    while (new_g == my_g_ || new_g == dst_g){
      new_g = random_number(dfly_->g(), attempt++, seed);
    }
    int min_dist = 3;
    int val_dist = 5;

    auto val_dest = group_gateways_[new_g][gateway_rotater_[new_g]];
    int min_port = group_ports_[dst_g][group_port_rotaters_[dst_g]];

    auto hdr = pkt->get_header<header>();
    bool go_valiant = switch_paths(min_dist, val_dist, min_port, val_dest.first);
    if (go_valiant){
      pkt->current_path().set_outport(val_dest.first);
      pkt->set_dest_switch(val_dest.second);
      gateway_rotater_[new_g] = (gateway_rotater_[new_g] + 1) % group_gateways_[new_g].size();
      hdr->stage_number = valiant_stage;
      rter_debug("chose inter-grp ugal port %d to intermediate %d : pkt=%p:%s",
                 val_dest.first, val_dest.second, pkt, pkt->to_string().c_str());
    } else { //minimal
      pkt->current_path().set_outport(min_port);
      pkt->set_dest_switch(dfly_->get_uid(dst_a,dst_g));
      gateway_rotater_[dst_g] = (gateway_rotater_[dst_g] + 1) % group_gateways_[dst_g].size();
      hdr->stage_number = minimal_stage;
      rter_debug("chose inter-grp minimal port %d: pkt=%p:%s",
                 min_port, pkt, pkt->to_string().c_str());
      group_port_rotaters_[dst_g] = (group_port_rotaters_[dst_g] + 1) % group_ports_[dst_g].size();
    }
  }

  void check_ugal_intra_group(packet* pkt, int dst_a, char minimal_stage){
    uint32_t seed = netsw_->now().ticks();
    uint32_t attempt = 0;
    int new_a = dst_a;
    while (new_a == my_a_ || new_a == dst_a){
      new_a = random_number(dfly_->a(), attempt++, seed);
    }
    int min_dist = 1;
    int val_dist = 2;

    auto hdr = pkt->get_header<header>();
    bool go_valiant = switch_paths(min_dist, val_dist, dst_a, new_a);
    if (go_valiant){
      pkt->current_path().set_outport(new_a);
      pkt->set_dest_switch(dfly_->get_uid(new_a, my_g_));
      hdr->stage_number = valiant_stage;
      rter_debug("chose intra-grp ugal port %d to intermediate %d : pkt=%p:%s",
                 new_a, int(pkt->dest_switch()), pkt, pkt->to_string().c_str());
    } else { //minimal
      pkt->current_path().set_outport(dst_a);
      pkt->set_dest_switch(dfly_->get_uid(dst_a, my_g_));
      hdr->stage_number = minimal_stage;
      rter_debug("chose inter-grp minimal port %d: pkt=%p:%s",
                 dst_a, pkt, pkt->to_string().c_str());
    }
  }

  void route(packet *pkt) override {
    uint16_t dir;
    switch_id ej_addr = top_->netlink_to_ejection_switch(pkt->toaddr(), dir);
    if (ej_addr == my_addr_){
      pkt->current_path().outport() = dir;
      pkt->current_path().vc = 0;
      return;
    }

    auto hdr = pkt->get_header<header>();
    switch(hdr->stage_number){
      case initial_stage: {
        int dst_a = dfly_->computeA(ej_addr);
        int dst_g = dfly_->computeG(ej_addr);
        if (dst_g == my_g_){
          check_ugal_intra_group(pkt, dst_a, minimal_only_stage);
        } else {
          check_ugal_inter_group(pkt, dst_a, dst_g, minimal_only_stage);
        }
        break;
      }
      case minimal_only_stage: {
        //don't reconsider my decision
        route_to_switch(pkt, ej_addr);
        rter_debug("continue to minimal %d on port %d: pkt=%p:%s",
                 ej_addr, pkt->current_path().outport(),
                 pkt, pkt->to_string().c_str());
        break;
      }
      case valiant_stage: {
        if (my_addr_ == pkt->dest_switch()){
          hdr->stage_number = final_stage;
        } else {
          route_to_switch(pkt, pkt->dest_switch());
          rter_debug("route to valiant intermediate %d on port %d: pkt=%p:%s",
                     (int(pkt->dest_switch())), pkt->current_path().outport(),
                     pkt, pkt->to_string().c_str());
          break;
        }
      }
      case final_stage: {
        pkt->set_dest_switch(ej_addr);
        route_to_switch(pkt, ej_addr);
        rter_debug("route to final %d on port %d: pkt=%p:%s",
                   ej_addr, pkt->current_path().outport(),
                   pkt, pkt->to_string().c_str());
        break;
      }
      break;
    }
    pkt->current_path().vc = hdr->num_hops;
    if (pkt->current_path().vc >= num_vc()){
      spkt_abort_printf("Packet %p:%s too many hops", pkt, pkt->to_string().c_str());
    }
    ++hdr->num_hops;
  }

 protected:
  int val_threshold_;

};
*/


}
}
