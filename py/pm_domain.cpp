//
// Copy particle positions to PM domains for density computation
// Retrive particle force from PM domains
//
#include <iostream>
#include <vector>
#include <deque>
#include <algorithm>
#include <cstdlib>
#include <cassert>
#include <mpi.h>

#include "msg.h"
#include "comm.h"
#include "util.h"
#include "pm.h"
#include "pm_domain.h"
#include "error.h"
//#include "hdf5_io.h"

using namespace std;

namespace {
  FFT const * fft = 0;
  int nc;
  int nbuf, nbuf_alloc;
  int nbuf_index, nbuf_index_alloc;
  MPI_Win win_nbuf, win_pos, win_force;
  Float x_left, x_right;
  Float* buf_pos= 0;
  Float* buf_force= 0;
  Index* buf_index= 0;
  vector<Domain> decomposition;
  deque<Packet>  packets_sent;
  Float3* packet_force;

  void allocate_pm_buffer(const size_t np_allocated, const double np_total,
			  const int local_nx);
  void allocate_decomposition(const Float boxsize, const int local_ix0,
				   const int local_nx);
  void packets_clear();
  void packets_flush();
}

static inline void send(const int i, const Float x[], const Float boxsize);

int Domain::packet_size= 1024/3*3;

void pm_domain_init(Particles const * const particles)
{
  // Initialise pm_domain module
  // May be called several times
  
  if(pm_get_fft() == 0) {
    msg_printf(msg_error,
	       "Error: pm_init must be called before pm_domain_init/pm_domain_send_positions\n");
    throw RuntimeError();
  }

  if(fft == pm_get_fft())
    return;  // already initialised and pm_fft stays the same

  pm_domain_free();  
  fft = pm_get_fft();

  // Initialise static variables  
  nc= fft->nc;

  const Float boxsize= particles->boxsize;
  x_left= boxsize/nc*(fft->local_ix0 + 1);
  x_right= boxsize/nc*(fft->local_ix0 + fft->local_nx - 1);

  allocate_pm_buffer(particles->np_allocated, particles->np_total,
		     fft->local_nx);

  allocate_decomposition(boxsize, fft->local_ix0, fft->local_nx);

  msg_printf(msg_verbose, "pm_domain initilised\n");
}

void pm_domain_free()
{
  if(fft == 0) return;
  
  MPI_Win_free(&win_nbuf);
  MPI_Win_free(&win_pos);
  MPI_Win_free(&win_force);

  free(buf_index);
  free(packet_force);
}


void pm_domain_send_positions(Particles* const particles)
{
  // Send particle positions to other nodes for PM density computation
  // Raises RuntimeError if pm module not initialised
  pm_domain_init(particles);
  assert(buf_pos);

  msg_printf(msg_verbose, "sending positions\n");

  nbuf= 0;
  nbuf_index= 0;
  packets_sent.clear();
  packets_clear();
  
  const int np= particles->np_local;
  const Float boxsize= particles->boxsize;

  MPI_Win_fence(0, win_pos);

  Particle* const p= particles->p;
  for(int i=0; i<np; ++i) {
    periodic_wrapup_p(p[i], boxsize);

    if(p[i].x[0] < x_left)
      send(i, p[i].x, boxsize);
    if(p[i].x[0] > x_right)
      send(i, p[i].x, boxsize);
  }

  packets_flush();

  MPI_Win_fence(0, win_pos);
}


void pm_domain_get_forces(Particles* const particles)
{
  // Get force from other nodes
  Float3* const f= particles->force;
  
  MPI_Win_fence(0, win_force);

  for(auto& packet : packets_sent) {
    const Index nsent= packet.n;

    MPI_Get(packet_force, 3*nsent, FLOAT_TYPE, packet.dest_rank,
	    packet.offset*3, 3*nsent, FLOAT_TYPE, win_force);

    Index index0= packet.offset_index;
    for(Index i=0; i<nsent; ++i) {
      Index ii= index0 + i;
#ifdef CHECK
      assert(0 <= ii && ii < nbuf_index);
#endif
      Index index= buf_index[ii];
      
      
#ifdef CHECK
      assert(0 <= index && index < particles->np_local);
#endif
      
      f[index][0] += packet_force[i][0];
      f[index][1] += packet_force[i][1];
      f[index][2] += packet_force[i][2];
    }
  }
  MPI_Win_fence(0, win_force);
}


/*
void pm_domain_write_packet_info(const char filename[])
{
  const int src_rank= comm_this_node();
  const int npackets= packets_sent.size();

  int* const dat= (int*) malloc(sizeof(int)*npackets*3); assert(dat);
  

  int i=0;
  for(auto& p : packets_sent) {
    dat[3*i]= src_rank;
    dat[3*i + 1]= p.dest_rank;
    dat[3*i + 2]= p.n;
    ++i;
  }

  hdf5_write_packet_data(filename, dat, npackets);
  free(dat);
}
*/


Pos const * pm_domain_buffer_positions()
{
  return (Pos const *) buf_pos;
}


Float3* pm_domain_buffer_forces()
{
  return (Float3*) buf_force;
}


int pm_domain_buffer_np()
{
  return nbuf;
}


namespace {

void allocate_pm_buffer(const size_t np_alloc, const double np_total,
			const int local_nx)
{
  // Create MPI Windows and allocate memory for buffers

  // nbuf: number of buffer particles in buf_pos, buf_force
  MPI_Win_create(&nbuf, sizeof(int), sizeof(int), MPI_INFO_NULL,
		 MPI_COMM_WORLD, &win_nbuf);

  int local_nx_max= comm_max<int>(local_nx);
  nbuf_alloc= 10 + 1.25*(np_total + 5*sqrt(np_total))/nc*(local_nx_max + 2);

  assert(nbuf_alloc > 0);

  // buf_pos: positions of particles from other MPI nodes
  MPI_Win_allocate(sizeof(Float)*3*nbuf_alloc,
		   sizeof(Float), MPI_INFO_NULL, MPI_COMM_WORLD,
		   &buf_pos, &win_pos);

  // buf_force: force at buf_pos
  MPI_Win_allocate(sizeof(Float)*3*nbuf_alloc,
		   sizeof(Float), MPI_INFO_NULL, MPI_COMM_WORLD,
		   &buf_force, &win_force);

  nbuf_index_alloc= np_alloc;
  buf_index= (Index*) malloc(sizeof(Index)*nbuf_index_alloc);

  // print memory used
  const size_t size_buf= sizeof(Float)*3*nbuf_alloc;
  const size_t size_index_buf= sizeof(Index)*nbuf_index_alloc;
  
  if(buf_pos == 0 || buf_force == 0 || buf_index == 0) {
    msg_printf(msg_fatal,
       "Error: unable to allocate %lu MBytes for PM domain buffer\n", size_buf);
    throw MemoryError();
  }

  msg_printf(msg_verbose,
	     "PM domain buffer %d MB allocated\n",
	     mbytes(2*size_buf + size_index_buf));
}

void allocate_decomposition(const Float boxsize, const int local_ix0,
			    const int local_nx)
{
  // Create the decomposition, a vector of domains.

  // Range of x that contribute to PM density
  const Float xbuf[2]= {boxsize*(local_ix0 - 1)/nc,
			boxsize*(local_ix0 + local_nx)/nc};
  const int n= comm_n_nodes();

  Float* const xbuf_all= (Float*) malloc(sizeof(Float)*2*n);
  assert(xbuf_all);

  MPI_Allgather(xbuf, 2, FLOAT_TYPE, xbuf_all, 2, FLOAT_TYPE, MPI_COMM_WORLD);

  const int n_dest= n - 1;
  const int this_node= comm_this_node();

  decomposition.clear();
  decomposition.reserve(n_dest);
  Domain d;
  for(int i=1; i<=n/2; ++i) {
    int i_plus= (this_node + i) % n;
    assert(i_plus != this_node);
    d.rank= i_plus;
    d.xbuf_min= xbuf_all[2*i_plus];
    d.xbuf_max= xbuf_all[2*i_plus + 1];
    decomposition.push_back(d);

    int i_minus= (this_node - i + n) % n;
    assert(i_minus != this_node);

    if(i_minus != i_plus) {
      d.rank= i_minus;
      d.xbuf_min= xbuf_all[2*i_minus];
      d.xbuf_max= xbuf_all[2*i_minus + 1];

      decomposition.push_back(d);
    }
  }
  free(xbuf_all);  
  assert(decomposition.size() == static_cast<size_t>(n_dest));

  assert(Domain::packet_size % 3 == 0);
  packet_force= (Float3*) malloc(sizeof(Float)*Domain::packet_size);
  assert(packet_force);
}


void packets_clear()
{
  for(auto& dom : decomposition)
    dom.clear();
}


void packets_flush()
{
  for(auto& dom : decomposition)
    dom.send_packet();
}

  
}

void Domain::clear()
{
  vbuf.clear();
  vbuf_index.clear();
}


void Domain::send_packet()
{
  assert(vbuf.size() % 3 == 0);
  const int nsend= vbuf.size() / 3;
  
  if(nsend == 0) {
    msg_printf(msg_debug, "No particle copy to node %d\n", rank);
    return;
  }
	 
  Packet pkt;
  pkt.dest_rank= rank;
  pkt.offset_index= nbuf_index;
  pkt.n= nsend;

  assert(0 <= nbuf_index && nbuf_index + nsend < nbuf_index_alloc);
  //for(auto ind= vbuf_index.begin(); ind != vbuf_index.end(); ++ind) {
  for(auto ind : vbuf_index) {
    assert(0 <= nbuf_index && nbuf_index < nbuf_index_alloc);
    buf_index[nbuf_index]= ind;
    nbuf_index++;
  }

  // offset= rank::nbuf
  // rank::nbuf += nsend
  int offset;
  MPI_Win_lock(MPI_LOCK_EXCLUSIVE, rank, 0, win_nbuf);
  MPI_Get_accumulate(&nsend, 1, MPI_INT, &offset, 1, MPI_INT,
		     rank, 0, 1, MPI_INT, MPI_SUM, win_nbuf);
  MPI_Win_unlock(rank, win_nbuf);

  if(offset + nsend >= nbuf_alloc) {
    msg_printf(msg_fatal,
	       "Error: pm buffer overflow: %d allocated, need more than %d\n",
	       nbuf_alloc, offset + nsend);
    throw RuntimeError();
  }

  // Copy positions to [nbuf, nbuf+nsend) in node 'rank'
  MPI_Put(&vbuf.front(), nsend*3, FLOAT_TYPE,
	  rank, offset*3, nsend*3, FLOAT_TYPE, win_pos);

  msg_printf(msg_debug, "sending packet: %d particles to %d, offset= %d\n",
	     nsend, rank, offset);
  
  // Record the information of position sending for later force get
  pkt.offset= offset;
  packets_sent.push_back(pkt);
  
  vbuf.clear();
  vbuf_index.clear();
}

void send(const int i, const Float x[], const Float boxsize)
{
  // ToDo: this is naive linear search all; many better way possible

  for(auto& dom : decomposition) {
    if((dom.xbuf_min < x[0] && x[0] < dom.xbuf_max) ||
       (dom.xbuf_min < x[0] - boxsize && x[0] - boxsize < dom.xbuf_max) ||
       (dom.xbuf_min < x[0] + boxsize && x[0] + boxsize < dom.xbuf_max)) {
      dom.push(i, x);
    }
  }
}

int pm_domain_nbuf()
{
  return nbuf;
}


void pm_domain_set_packet_size(const int packet_size)
{
  if(buf_pos) {
    msg_printf(msg_error,
	       "Error: pm_domain already initialised. "
	       "packet_size must be set earlier\n");
    throw RuntimeError();
  }

  Domain::packet_size= packet_size/3*3;

  msg_printf(msg_verbose, "Domain::packet_size set to %d\n",
	     Domain::packet_size);
}

