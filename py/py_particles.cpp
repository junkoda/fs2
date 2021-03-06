#include <iostream>
#include <vector>
#include <typeinfo>
#include "msg.h"
#include "particle.h"
#include "util.h"
#include "comm.h"
#include "py_particles.h"
#include "py_assert.h"

#define NPY_NO_DEPRECATED_API NPY_1_7_API_VERSION
#include "numpy/arrayobject.h"

#ifdef DOUBLEPRECISION
#define NPY_FLOAT_TYPE NPY_DOUBLE
#else
#define NPY_FLOAT_TYPE NPY_FLOAT
#endif

using namespace std;

static vector<Particle>* v= 0;

PyMODINIT_FUNC
py_particles_module_init()
{
  import_array();

  return NULL;
}

static int npy_type_num(const type_info& type_id)
{
  if(type_id == typeid(float))
    return NPY_FLOAT;
  else if(type_id == typeid(double))
    return NPY_DOUBLE;
  else if(type_id == typeid(uint64_t))
    return NPY_UINT64;
  else {
    msg_printf(msg_fatal, "Error: unknown typeid for npy_num_num");
    throw RuntimeError();
  }

  return 0;
}

//
// Template functions
//
template <class T>
PyObject* py_particles_asarray(T const * dat,
			       const size_t np_local, const int ncol,
			       const size_t stride_size)
{
  // T Float or uint64_t
  
  // Allocate a new np.array
  PyObject* arr= 0;
  T* recvbuf= 0;

  const int typenum= npy_type_num(typeid(T));

  size_t np_total= comm_sum<long long>(np_local);
      
  if(comm_this_node() == 0) {
    const int nd= ncol == 1 ? 1 : 2;
    npy_intp dims[]= {npy_intp(np_total), ncol};

    arr= PyArray_ZEROS(nd, dims, typenum, 0);
    py_assert_ptr(arr);
  
    recvbuf= (T*) PyArray_DATA((PyArrayObject*) arr);
    py_assert_ptr(recvbuf);
  }

  //
  // Gather global grid data if n_nodes > 1
  //

    
  const size_t nsend= np_local;

  T* const sendbuf= (T*) malloc(sizeof(T)*nsend*ncol);
  py_assert_ptr(sendbuf);

  size_t ibuf= 0;

  for(size_t i=0; i<nsend; ++i) {
    for(int j=0; j<ncol; ++j) {
      sendbuf[ibuf++]= dat[j];
    }
    
    dat = (T*) (((char*)dat) + stride_size);
  }

  const int n= comm_n_nodes();
  int* nrecv= 0;
  int* displ=0;

  if(comm_this_node() == 0) {
    // Allocate for gathered data
    nrecv= (int*) malloc(sizeof(int)*2*n);
    if(nrecv == 0) {
      PyErr_SetString(PyExc_MemoryError,
		      "Unable to allocate memory for nrecv");
      return NULL;
    }

    displ= nrecv + n;
  }

  MPI_Gather(&nsend, 1, MPI_INT, nrecv, 1, MPI_INT, 0,
	     MPI_COMM_WORLD);

  if(comm_this_node() == 0) {
    int displ_sum= 0;
    for(int i=0; i<n; ++i) {
      nrecv[i] *= sizeof(T)*ncol;
      displ[i]= displ_sum;
      displ_sum += nrecv[i];
    }
  }

  // warning: MPI_Gatherv uses int
  // Error beyond more than 2^32 bytes of data for 4-byte int
  MPI_Gatherv(sendbuf, nsend*sizeof(T)*ncol, MPI_BYTE, 
	      recvbuf, nrecv, displ, MPI_BYTE, 0, MPI_COMM_WORLD);

  //cerr << "free nrecv\n";  free(nrecv);
  //cerr << "free sndbuf\n"; free(sendbuf);

  if(comm_this_node() == 0) {
    return arr;
  }

  Py_RETURN_NONE;
}

//
//
//
PyObject* py_particles_alloc(PyObject* self, PyObject* args)
{
  unsigned long long np;
  double boxsize;
  
  if(!PyArg_ParseTuple(args, "Kd", &np, &boxsize)) {
    return NULL;
  }

  Particles* const particles = new Particles((size_t)np, boxsize);

  return PyCapsule_New(particles, "_Particles", py_particles_free);
}

void py_particles_free(PyObject *obj)
{
  Particles* const particles=
    (Particles *) PyCapsule_GetPointer(obj, "_Particles");
  py_assert_void(particles);

  delete particles;
}

PyObject* py_particles_len(PyObject* self, PyObject* args)
{
  PyObject* py_particles;
  
  if(!PyArg_ParseTuple(args, "O", &py_particles))
     return NULL;

  Particles* const particles=
    (Particles *) PyCapsule_GetPointer(py_particles, "_Particles");
  py_assert_ptr(particles);

  return Py_BuildValue("k", (unsigned long) particles->np_local);
}

PyObject* py_particles_slice(PyObject* self, PyObject* args)
{
  // _particles_slice(_particles, frac)
  // Return particles 0 < x[2] < frac*boxsize as an np.array
  PyObject* py_particles;
  double frac;
  
  if(!PyArg_ParseTuple(args, "Od", &py_particles, &frac))
     return NULL;

  Particles* const particles=
    (Particles *) PyCapsule_GetPointer(py_particles, "_Particles");
  py_assert_ptr(particles);

  const Float boxsize= particles->boxsize;
  const Float x_max= frac*boxsize;
  Particle* const p= particles->p;
  const size_t n= particles->np_local;

  if(v == 0) v= new vector<Particle>();
  v->clear();

  for(size_t i=0; i<n; ++i) {
    Particle pp = p[i];
    periodic_wrapup_p(pp, boxsize);
    if(pp.x[2] < x_max)   
      v->push_back(pp);
  }

  // Return vector<Particle> as np.array
  const int nd=2;
  const int ncol= sizeof(Particle)/sizeof(Float);
  npy_intp dims[]= {(npy_intp) v->size(), ncol};

  return PyArray_SimpleNewFromData(nd, dims, NPY_FLOAT_TYPE, &(v->front()));
}

PyObject* py_particles_getitem(PyObject* self, PyObject* args)
{
  if(v == 0) v= new vector<Particle>();
  v->clear();
  
  PyObject *py_particles, *py_index;
  
  if(!PyArg_ParseTuple(args, "OO", &py_particles, &py_index))
    return NULL;

  Particles* const particles=
    (Particles *) PyCapsule_GetPointer(py_particles, "_Particles");
  py_assert_ptr(particles);
  Particle* const p= particles->p;
  const size_t n= particles->np_local;

  if(PyNumber_Check(py_index)) {
    // particles[i]; return ith particle
    long i= PyLong_AsLong(py_index);
    if(i < 0)
      i = n + i;

    if(i < 0 || i >= (long) particles->np_local) {
      PyErr_SetNone(PyExc_IndexError);
      return NULL;
    }
    
    return Py_BuildValue("(ddd)",
		 (double) p[i].x[0], (double) p[i].x[1], (double) p[i].x[2]);
  }
  else if(PySlice_Check(py_index)) {
    // particles[i:j:k]; return i to j with step k
    cout << "Slice object given\n";
    Py_ssize_t start, stop, step, length;
    const int ret=
      PySlice_GetIndicesEx(py_index, n, &start, &stop, &step, &length);
    if(ret)
      return NULL;

    for(int i=start; i<stop; i+=step) {
      if(0 <= i && i < (int) particles->np_local)
	v->push_back(p[i]);
    }
  }
  else {
    return NULL;
  }

  const int nd=2;
  const int ncol= sizeof(Particle)/sizeof(Float);
  npy_intp dims[]= {(npy_intp) v->size(), ncol};

  return PyArray_SimpleNewFromData(nd, dims, NPY_FLOAT, &(v->front()));
}


PyObject* py_particles_update_np_total(PyObject* self, PyObject* args)
{
  // _update_np_total(_particles)
  PyObject* py_particles;
  
  if(!PyArg_ParseTuple(args, "O", &py_particles))
     return NULL;

  Particles* const particles=
    (Particles *) PyCapsule_GetPointer(py_particles, "_Particles");
  py_assert_ptr(particles);

  particles->update_np_total();

  Py_RETURN_NONE;
}


PyObject* py_particles_id_asarray(PyObject* self, PyObject* args)
{
  PyObject* py_particles;
  if(!PyArg_ParseTuple(args, "O", &py_particles))
    return NULL;

  Particles const * const particles=
    (Particles const *) PyCapsule_GetPointer(py_particles, "_Particles");
  py_assert_ptr(particles);

  return py_particles_asarray<uint64_t>(&particles->p->id,
					particles->np_local, 1,
					sizeof(Particle));
}

PyObject* py_particles_x_asarray(PyObject* self, PyObject* args)
{
  //
  //
  // x_type: 0 x
  //         1 dx1
  //         2 dx2
  //
  PyObject* py_particles;
  int x_type;
  
  if(!PyArg_ParseTuple(args, "Oi", &py_particles, &x_type))
    return NULL;


  Particles const * const particles=
    (Particles const *) PyCapsule_GetPointer(py_particles, "_Particles");
  py_assert_ptr(particles);

  Float* p = 0;
  if(x_type == 0) {
    p = particles->p[0].x;
  }
  else if(x_type == 1) {
    p = particles->p[0].dx1;
  }
  else if(x_type == 2)
    p = particles->p[0].dx2;
  else
    py_assert_ptr(false);

  return py_particles_asarray<Float>(p, particles->np_local, 3,
				     sizeof(Particle));

  return 0;
}

PyObject* py_particles_force_asarray(PyObject* self, PyObject* args)
{
  PyObject* py_particles;
  if(!PyArg_ParseTuple(args, "O", &py_particles))
    return NULL;

  Particles const * const particles=
    (Particles const *) PyCapsule_GetPointer(py_particles, "_Particles");
  py_assert_ptr(particles);

  return py_particles_asarray<Float>((Float*) particles->force,
				     particles->np_local, 3, sizeof(Float)*3);
}


PyObject* py_particles_np_total(PyObject* self, PyObject* args)
{
  PyObject* py_particles;
  if(!PyArg_ParseTuple(args, "O", &py_particles))
    return NULL;

  Particles const * const particles=
    (Particles const *) PyCapsule_GetPointer(py_particles, "_Particles");
  py_assert_ptr(particles);

  return Py_BuildValue("k", (unsigned long) particles->np_total);
}

PyObject* py_particles_periodic_wrapup(PyObject* self, PyObject* args)
{
  PyObject* py_particles;
  if(!PyArg_ParseTuple(args, "O", &py_particles))
    return NULL;

  Particles* const particles=
    (Particles*) PyCapsule_GetPointer(py_particles, "_Particles");
  py_assert_ptr(particles);

  util_periodic_wrapup(particles);

  Py_RETURN_NONE;
}

PyObject* py_particles_clear(PyObject* self, PyObject* args)
{
  PyObject *py_particles;
  if(!PyArg_ParseTuple(args, "O", &py_particles))
    return NULL;
  
  Particles* const particles=
    (Particles*) PyCapsule_GetPointer(py_particles, "_Particles");
  py_assert_ptr(particles);

  particles->pv->clear();
  particles->np_local= particles->pv->size();
  particles->update_np_total();

  Py_RETURN_NONE;
}
  
PyObject* py_particles_append(PyObject* self, PyObject* args)
{
  PyObject *py_particles, *py_arr;
  if(!PyArg_ParseTuple(args, "OO", &py_particles, &py_arr))
    return NULL;

  Particles* const particles=
    (Particles*) PyCapsule_GetPointer(py_particles, "_Particles");
  py_assert_ptr(particles);

  if(py_arr != Py_None) {
    Py_buffer view;

    if(PyObject_GetBuffer(py_arr, &view,
			  PyBUF_ANY_CONTIGUOUS | PyBUF_FORMAT) == -1) {
      return NULL;
    }

    if(view.ndim != 2) {
      PyErr_SetString(PyExc_TypeError, "Expected a 2-dimensional array");
      PyBuffer_Release(&view);
      return NULL;
    }
    if(strcmp(view.format, "d") != 0) {
      PyErr_SetString(PyExc_TypeError, "Expected an array of doubles");
      PyBuffer_Release(&view);
      return NULL;
    }
    
    const int n= view.shape[0];
    const int ncol= view.shape[1];
    
    if(ncol < 3) {
      PyErr_SetString(PyExc_TypeError, "Expected at least 3 columns for x y z");
      return NULL;
    }
    
    const size_t next_row= view.strides[0]/sizeof(double);
    const size_t next_col= view.strides[1]/sizeof(double);
    
    double* arr= (double*) view.buf;
    const size_t n_new= particles->np_local + n;
    
    for(size_t i= particles->np_local; i<n_new; ++i) {
      Float* x= particles->pv->at(i).x;
      x[0]= *arr;
      x[1]= *(arr + next_col);
      x[2]= *(arr + 2*next_col);
      
      arr += next_row;
    }
    
    PyBuffer_Release(&view);

    particles->np_local= n_new;
  }
  particles->update_np_total();
      
  Py_RETURN_NONE;
}
