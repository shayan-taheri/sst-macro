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

#include <sstmac/hardware/network/network_message.h>
#include <sprockit/errors.h>

#define enumcase(x) case x: return #x;

namespace sstmac {
namespace hw {

NetworkMessage::~NetworkMessage()
{
  if (wire_buffer_){
    delete[] (char*) wire_buffer_;
  }
  if (smsg_buffer_){
    delete[] (char*) smsg_buffer_;
  }
}

bool
NetworkMessage::isNicAck() const
{
  switch(type_)
  {
  case payload_sent_ack:
  case rdma_put_sent_ack:
  case rdma_get_sent_ack:
    return true;
  default:
    return false;
  }
}

void
NetworkMessage::putOnWire()
{
  switch(type_){
    case rdma_get_payload:
    case nvram_get_payload:
      putBufferOnWire(remote_buffer_, payload_bytes_);
      break;
    case rdma_put_payload:
      putBufferOnWire(local_buffer_, payload_bytes_);
      break;
    case payload:
      putBufferOnWire(smsg_buffer_, byteLength());
      smsg_buffer_ = nullptr;
      break;
    default:
      break; //nothing to do
  }
}

void
NetworkMessage::putBufferOnWire(void* buf, uint64_t sz)
{
  if (buf){
    wire_buffer_ = new char[sz];
    ::memcpy(wire_buffer_, buf, sz);
  }
}

void
NetworkMessage::takeBufferOffWire(void *buf, uint64_t sz)
{
  if (buf){
    ::memcpy(buf, wire_buffer_, sz);
    delete[] (char*) wire_buffer_;
    wire_buffer_ = nullptr;
  }
}

void
NetworkMessage::takeOffWire()
{
  switch (type_){
    case rdma_get_payload:
      takeBufferOffWire(local_buffer_, payload_bytes_);
      break;
    case rdma_put_payload:
      takeBufferOffWire(remote_buffer_, payload_bytes_);
      break;
    case payload:
      smsg_buffer_ = wire_buffer_;
      wire_buffer_ = nullptr;
      break;
    default:
      break;
  }
}

void
NetworkMessage::intranode_memmove()
{
  switch (type()){
    case rdma_get_payload:
      memmoveRemoteToLocal();
      break;
    case rdma_put_payload:
      memmoveLocalToRemote();
      break;
    case payload:
      putBufferOnWire(smsg_buffer_, byteLength());
      smsg_buffer_ = wire_buffer_;
      wire_buffer_ = nullptr;
      break;
    default:
      break;
  }
}

void
NetworkMessage::memmoveLocalToRemote()
{
  //due to scatter-gather elements, it's now allowed
  //to have a null remote buffer
  if (local_buffer_ && remote_buffer_){ //might be null
    ::memcpy(remote_buffer_, local_buffer_, payload_bytes_);
  }
}

void
NetworkMessage::memmoveRemoteToLocal()
{
  //due to scatter-gather elements, it's now allowed
  //to have a null local buffer
  if (remote_buffer_ && local_buffer_){
    ::memcpy(local_buffer_, remote_buffer_, payload_bytes_);
  }
}


void
NetworkMessage::convertToAck()
{
  reverse();
  switch(type_)
  {
    case rdma_get_payload:
      type_ = rdma_get_sent_ack;
      break;
    case rdma_put_payload:
      type_ = rdma_put_sent_ack;
      break;
    case payload:
      type_ = payload_sent_ack;
      break;
    default:
      spkt_abort_printf("NetworkMessage::clone_injection_ack: cannot ack msg type %s", tostr(type_));
      break;
  }
  Flow::byte_length_ = 32;
  wire_buffer_ = nullptr;
  smsg_buffer_ = nullptr;
}

void
NetworkMessage::reverse()
{
  //also flip the addresses
  NodeId dst = fromaddr_;
  NodeId src = toaddr_;
  toaddr_ = dst;
  fromaddr_ = src;
}

const char*
NetworkMessage::tostr(nic_event_t mut)
{
  switch (mut) {
      enumcase(RDMA_GET_REQ_TO_RSP);
      enumcase(RDMA_GET_FAILED);
      enumcase(NVRAM_GET_REQ_TO_RSP);
  }
  spkt_throw_printf(sprockit::ValueError,
       "NetworkMessage: invalid nic event %d", mut);
}

const char*
NetworkMessage::tostr(type_t ty)
{
  switch(ty)
  {
      enumcase(payload);
      enumcase(payload_sent_ack);
      enumcase(rdma_get_request);
      enumcase(rdma_get_payload);
      enumcase(rdma_get_sent_ack);
      enumcase(rdma_get_nack);
      enumcase(rdma_put_payload);
      enumcase(rdma_put_sent_ack);
      enumcase(rdma_put_nack);
      enumcase(null_netmsg_type);
      enumcase(nvram_get_request);
      enumcase(nvram_get_payload);
      enumcase(failure_notification);
  }
  spkt_throw_printf(sprockit::ValueError,
    "NetworkMessage::tostr: unknown type_t %d",
    ty);
}

void
NetworkMessage::serialize_order(serializer& ser)
{
  Flow::serialize_order(ser);
  ser & needs_ack_;
  ser & toaddr_;
  ser & fromaddr_;
  ser & payload_bytes_;
  ser & type_;
  ser & aid_;
  if (type_ == null_netmsg_type){
    spkt_abort_printf("failed serializing network message - got null type");
  }
  ser.primitive(remote_buffer_);
  ser.primitive(local_buffer_);
  ser.primitive(smsg_buffer_);
  //then there will be a wire buffer
  if (remote_buffer_ || local_buffer_ || smsg_buffer_){
    ser & sstmac::array(wire_buffer_, payload_bytes_);
  }
}

bool
NetworkMessage::isMetadata() const
{
  switch(type_)
  {
    case nvram_get_request:
    case rdma_get_request:
    case rdma_get_sent_ack:
    case rdma_put_sent_ack:
    case payload_sent_ack:
    case rdma_get_nack:
    case rdma_put_nack:
    case failure_notification:
    case null_netmsg_type:
      return true;
    case payload:
    case rdma_get_payload:
    case nvram_get_payload:
    case rdma_put_payload:
      return false;
    default:
     spkt_abort_printf("Bad message type %d", type_);
     return false; //make gcc happy
  }
}

void
NetworkMessage::nicReverse(type_t newtype)
{
  //hardware level reverse only
  NetworkMessage::reverse();
  type_ = newtype;
  switch(newtype){
  case rdma_get_payload:
    setFlowSize(payload_bytes_);
    break;
  default: break;
  }
}

/**
void
NetworkMessage::clone_into(NetworkMessage* cln) const
{
  cln->needs_ack_ = needs_ack_;
  cln->toaddr_ = toaddr_;
  cln->fromaddr_ = fromaddr_;
  cln->flow_id_ = flow_id_;
  cln->bytes_ = bytes_;
  cln->type_ = type_;
}
*/

}
}
