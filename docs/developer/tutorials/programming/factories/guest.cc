#include "guest.h"

namespace sstmac {
namespace tutorial {

christopher_guest::christopher_guest(SST::Params& params) :
  actor(params)
{
  num_fingers_ = params.find<int>("num_fingers");
  if (num_fingers_ != 6) {
    spkt_throw_printf(sprockit::ValueError,
                     "invalid number of fingers %d - must be 6");
  }
}

void
christopher_guest::act()
{
  std::cout << "You've been chasing me your entire life only to fail now.\n"
            << "I think that's the worst thing I've ever heard. How marvelous."
            << std::endl;
}

}
}
