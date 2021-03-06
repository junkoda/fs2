#include <iostream>
#include <vector>
#include <cmath>
#include <cassert>
#include <hdf5.h>

#include "config.h"
#include "msg.h"
#include "comm.h"
#include "cola.h"
#include "cosmology.h"
#include "hdf5_io.h"

using namespace std;

namespace {
  void write_header(const hid_t loc, Particles const * const particles);

  void write_data_double(hid_t loc, const char name[], const double val);
  void write_data_int(hid_t loc, const char name[], const int val);
  void write_data_table(hid_t loc, const char name[], 
			const hsize_t nrow, const hsize_t ncol,
			const hsize_t stride,
			const hid_t mem_type, const hid_t save_type,
			void const * const data);
}

void hdf5_write_particles(const char filename[],
			  Particles const * const particles,
			  char const* var)
{
  // var is a subset of "ixvf12" (in this order)
  // which specifies which data (position, velocity, ..) are writen
  //  i: id
  //  x: position
  //  v: velocity  [internal velocity, i.e., for cola without 2LPT velocity]
  //  c: cola velocity
  //  f: force
  //  1: 1LPT displacements (at a=1)
  //  2: 2LPT displacements (at a=1)

  H5Eset_auto2(H5E_DEFAULT, NULL, 0);
    
  // Parallel file access
  hid_t plist= H5Pcreate(H5P_FILE_ACCESS);
  H5Pset_fapl_mpio(plist, MPI_COMM_WORLD, MPI_INFO_NULL);

  hid_t file= H5Fopen(filename, H5F_ACC_RDWR, plist);
  if(file < 0) {
    file= H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, plist);
    if(file < 0) {
      msg_printf(msg_error, "Error: unable to create HDF5 file, %s\n",
		 filename);
      throw IOError();
    }
    
    msg_printf(msg_debug, "Created a new HDF5 file, %s\n", filename);
  }
  else {
    msg_printf(msg_debug, "Opened HDF5 file, %s\n", filename);
  }
  
  Particle const * const p= particles->p;
  const size_t np= particles->np_local;

  msg_printf(msg_verbose, "writing header\n");
  write_header(file, particles);

  if(*var == 'i') {
    msg_printf(msg_verbose, "writing ids\n");
    assert(sizeof(Particle) % sizeof(uint64_t) == 0);

    const size_t istride= sizeof(Particle)/sizeof(uint64_t);
    write_data_table(file, "id", np, 1, istride,
		     H5T_NATIVE_UINT64, H5T_STD_U64LE, &p->id);
    ++var;
  }

  static_assert(sizeof(Particle) % sizeof(Float) == 0,
		"Error: Sizeof(Particle) is not a multiple of sizeof(Float).");
  const size_t stride= sizeof(Particle)/sizeof(Float);
    
  if(*var == 'x') {
    msg_printf(msg_verbose, "writing positions\n");
    write_data_table(file, "x", np, 3, stride,
		     FLOAT_MEM_TYPE, FLOAT_SAVE_TYPE, p->x);
    ++var;
  }

  if(*var == 'v') {
    msg_printf(msg_verbose, "writing raw velocities\n");
    write_data_table(file, "v", np, 3, stride,
		     FLOAT_MEM_TYPE, FLOAT_SAVE_TYPE, p->v);
    ++var;
  }

  if(*var == 'c') {
    msg_printf(msg_verbose, "writing cola adjusted velocities\n");
    vector<Float> v= cola_velocity(particles);

    
    write_data_table(file, "v", np, 3, 3,
		     FLOAT_MEM_TYPE, FLOAT_SAVE_TYPE, &v.front());
    ++var;
  }


  if(*var == 'f') {
    msg_printf(msg_verbose, "writing forces\n");
    write_data_table(file, "f", np, 3, 3,
		     FLOAT_MEM_TYPE, FLOAT_SAVE_TYPE, particles->force);
    
    ++var;
  }

  if(*var == '1') {
    msg_printf(msg_verbose, "writing 1st-order LPT displacements\n");    
    write_data_table(file, "dx1", np, 3, stride,
		     FLOAT_MEM_TYPE, FLOAT_SAVE_TYPE, p->dx1);
    
    ++var;
  }

  if(*var == '2') {
    msg_printf(msg_verbose, "writing 2nd-order LPT displacements\n");    
    write_data_table(file, "dx2", np, 3, stride,
		     FLOAT_MEM_TYPE, FLOAT_SAVE_TYPE, p->dx2);
    
    ++var;
  }

  if(*var != '\0') {
    msg_printf(msg_error, "Error: unknown option for hdf5_write, %s\n", var);
    throw ValError();
  }

  H5Pclose(plist);
  H5Fclose(file);
}


void hdf5_write_packet_data(const char filename[], const int data[], const int n)
{
  // Parallel file access
  hid_t plist= H5Pcreate(H5P_FILE_ACCESS);
  H5Pset_fapl_mpio(plist, MPI_COMM_WORLD, MPI_INFO_NULL);

  hid_t file= H5Fcreate(filename, H5F_ACC_TRUNC, H5P_DEFAULT, plist);
  if(file < 0) {
    msg_printf(msg_error, "Error: unable to create HDF5 file, %s\n", filename);
    throw IOError();
  }

  write_data_table(file, "packet", n, 3, 3, H5T_NATIVE_INT, H5T_STD_I32LE, data);

  H5Pclose(plist);
  H5Fclose(file);
}


namespace {
void write_header(const hid_t loc, Particles const * const particles)
{
  //hid_t plist = H5Pcreate(H5P_DATASET_XFER);
  //H5Pset_dxpl_mpio(plist, H5FD_MPIO_COLLECTIVE);
  const char group_name[]= "parameters";
  
  const hid_t group= H5Gcreate(loc, group_name,
			 H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  
  if(group < 0) {
    msg_printf(msg_error, "Error: unable to open group, %s\n", group_name);
      throw IOError();
  }

  write_data_double(group, "boxsize", particles->boxsize);
  write_data_double(group, "omega_m", cosmology_omega_m());
  write_data_double(group, "ax", particles->a_x);
  write_data_double(group, "av", particles->a_v);

  const int nc= round(pow((double) particles->np_total, 1.0/3.0));
  write_data_int(group, "nc", nc);


  herr_t status= H5Gclose(group);
  assert(status >= 0);
}



void write_data_int(hid_t loc, const char name[], const int val)
{
  const hid_t scalar= H5Screate(H5S_SCALAR);
  hid_t data= H5Dcreate(loc, name, H5T_STD_I32LE, scalar, 
			H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if(data < 0) {
    msg_printf(msg_error, "Error: unable to create int data, %s\n", name);
    throw IOError();
  }

  if(comm_this_node() == 0) {
    herr_t status= H5Dwrite(data, H5T_NATIVE_INT, scalar, H5S_ALL,
			    H5P_DEFAULT, &val);
    assert(status >= 0);
  }

  H5Dclose(data);
  H5Sclose(scalar);
}

void write_data_double(hid_t loc, const char name[], const double val)
{
  const hid_t scalar= H5Screate(H5S_SCALAR);
  hid_t data= H5Dcreate(loc, name, H5T_IEEE_F64LE, scalar, 
			H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if(data < 0) {
    msg_printf(msg_error, "Error: unable to create float data, %s\n", name);
    throw IOError();
  }

  if(comm_this_node() == 0) {
    herr_t status= H5Dwrite(data, H5T_NATIVE_DOUBLE, scalar, H5S_ALL,
			    H5P_DEFAULT, &val);
  
    assert(status >= 0);
  }

  H5Dclose(data);
  H5Sclose(scalar);
}


void write_data_table(hid_t loc, const char name[], 
		      const hsize_t nrow, const hsize_t ncol,
		      const hsize_t stride,
		      const hid_t mem_type, const hid_t save_type,
		      void const * const data)
{
  // Float    FLOAT_MEM_TYPE    FLOAT_SAVE_TYPE (config.h)
  // uint64_t H5T_NATIVE_UINT64 H5T_STD_U64LE 
  
  // Gather inter-node information
  long long offset_ll= comm_partial_sum<long long>(nrow);
  offset_ll -= nrow;

  long long nrow_total= comm_sum<long long>(nrow);
  if(nrow_total == 0) {
    msg_printf(msg_warn, "Warning: zero data given to write_data_table\n"); 
    return;
  }


  // Data structure in memory
  const hsize_t data_size_mem= nrow*stride;
  hid_t memspace= H5Screate_simple(1, &data_size_mem, 0);

  const hsize_t offset_mem= 0;
  const hsize_t size_mem= ncol;
  const hsize_t count_mem= nrow;
  H5Sselect_hyperslab(memspace, H5S_SELECT_SET,
		      &offset_mem, &stride, &count_mem, &size_mem);

  // Data structure in file
  const hsize_t dim= ncol == 1 ? 1 : 2;
  const hsize_t data_size_file[]= {hsize_t(nrow_total), ncol};
  hid_t filespace= H5Screate_simple(dim, data_size_file, NULL);

  // local subset of data for this node
  const hsize_t offset_file[]= {hsize_t(offset_ll), 0};
  const hsize_t count_file[]= {nrow, ncol};

  H5Sselect_hyperslab(filespace, H5S_SELECT_SET,
		      offset_file, NULL, count_file, NULL);

  hid_t dataset= H5Dcreate(loc, name, save_type, filespace,
			   H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
  if(dataset < 0)
    return;

  hid_t plist= H5Pcreate(H5P_DATASET_XFER);
  H5Pset_dxpl_mpio(plist, H5FD_MPIO_COLLECTIVE);
    
  const herr_t status = H5Dwrite(dataset, mem_type, memspace, filespace,
				 plist, data);

  H5Pclose(plist);
  H5Sclose(memspace);
  H5Sclose(filespace);
  H5Dclose(dataset);
  
  assert(status >= 0);
}

}
