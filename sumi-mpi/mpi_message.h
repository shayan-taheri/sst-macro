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

#ifndef SSTMAC_SOFTWARE_LIBRARIES_MPI_MPIMESSAGE_H_INCLUDED
#define SSTMAC_SOFTWARE_LIBRARIES_MPI_MPIMESSAGE_H_INCLUDED

#include <sstmac/common/sstmac_config.h>
#include <sumi-mpi/mpi_status.h>
#include <sumi-mpi/mpi_types/mpi_type.h>
#include <sumi-mpi/mpi_integers.h>
#include <sstmac/software/process/task_id.h>
#include <sstmac/software/process/app_id.h>

#include <sstmac/software/process/operating_system_fwd.h>
#include <sumi-mpi/mpi_protocol/mpi_protocol_fwd.h>
#include <sumi/message.h>
#include <sprockit/thread_safe_new.h>

namespace sumi {

class mpi_message final :
  public sumi::deprecated::message,
  public sprockit::thread_safe_new<mpi_message>
{
  ImplementSerializable(mpi_message)

 public:
  typedef uint64_t id;

  typedef enum {
    null_content, data, header, eager_payload, completion_ack, fake
  } content_type_t;

  void recompute_bytes();

 public:
  mpi_message(int src, int dst, int count,
              MPI_Datatype type, int type_packed_size,
              int tag,
              MPI_Comm commid, int seqnum,
              mpi_message::id msgid,
              mpi_protocol* protocol);

  mpi_message(){}

  std::string to_string() const override;

  static const char* str(content_type_t content_type);

  ~mpi_message() throw ();

  sumi::mpi_message* clone_me() const {
    mpi_message* cln = new mpi_message;
    clone_into(cln);
    return cln;
  }

  sumi::deprecated::message* clone(payload_type_t ty) const override {
    return clone_me();
  }

  void serialize_order(sstmac::serializer& ser) override;

  uint64_t payload_bytes() const {
    return uint64_t(count_) * uint64_t(type_packed_size_);
  }

  mpi_protocol* protocol() const;

  void set_protocol(mpi_protocol* protocol);

  void payload_to_completion_ack();

  int count() const {
    return count_;
  }


  bool is_payload() const {
    switch (content_type_){
  case eager_payload:
  case data:
    return true;
  default:
    return false;
    }
  }

  MPI_Datatype type() const {
    return type_;
  }

  int type_packed_size() const {
    return type_packed_size_;
  }

  int tag() const {
    return tag_;
  }

  MPI_Comm comm() const {
    return commid_;
  }

  int seqnum() const {
    return seqnum_;
  }

  int src_rank() const {
    return src_rank_;
  }

  void set_src_rank(int rank) {
    src_rank_ = rank;
  }

  int dst_rank() const {
    return dst_rank_;
  }

  void set_dst_rank(int rank) {
    dst_rank_ = rank;
  }

  mpi_message::id unique_int() const {
    return msgid_;
  }

  content_type_t content_type() const {
    return content_type_;
  }

  void set_content_type(content_type_t ty) {
    content_type_ = ty;
    recompute_bytes();
  }

  void build_status(MPI_Status* stat) const;

  void set_in_flight(bool flag){
    in_flight_ = flag;
  }

  bool in_flight() const {
    return in_flight_;
  }

 protected:
  void clone_into(mpi_message* cln) const;

 protected:
  int src_rank_;
  int dst_rank_;
  int count_;
  MPI_Datatype type_;
  int type_packed_size_;
  int tag_;
  MPI_Comm commid_;
  int seqnum_;
  mpi_message::id msgid_;
  content_type_t content_type_;
  int protocol_;
  bool in_flight_;

};

}

#endif
