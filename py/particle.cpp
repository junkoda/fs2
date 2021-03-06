#include <cstdlib>
#include <cassert>
#include <mpi.h>

#include "msg.h"
#include "comm.h"
#include "util.h"
#include "fft.h"
#include "particle.h"

using namespace std;

Particles::Particles(const size_t np_alloc, const double boxsize_) :
  a_f(0.0), np_local(0), np_total(0), boxsize(boxsize_)
{
  //size_t nx= fft_local_nx(nc);
  //size_t np_alloc= (size_t)((1.25*(nx + 1)*nc*nc));

  pv= new vector<Particle>(np_alloc);
  p= &pv->front();

  force= (Float3*) calloc(3*np_alloc, sizeof(Float)); assert(force);
  np_allocated= np_alloc;
  
  msg_printf(msg_verbose, "%lu Mbytes allocated for %lu particles\n",
	     mbytes(np_alloc*sizeof(Particle)), np_alloc);
}


Particles::~Particles()
{

}

void Particles::update_np_total()
{
  // MPI Communicate and update total number of particles
  np_total= comm_sum<long long>(np_local);

  msg_printf(msg_debug, "Update np_total(%d) = %lu\n", comm_this_node(), np_total);
}
