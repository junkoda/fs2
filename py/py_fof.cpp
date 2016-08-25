#include <cmath>
#include "config.h"
#include "particle.h"
#include "comm.h"
#include "kdtree.h"
#include "fof.h"
#include "py_fof.h"
#include "py_array.h"
#include "py_assert.h"

using namespace std;


PyObject* py_fof_find_groups(PyObject* self, PyObject* args)
{
  // _fof_find_groups(_particles, linking_length, quota)

  PyObject *py_particles, *py_boxsize3;
  double linking_length;
  int quota;
  if(!PyArg_ParseTuple(args, "OdOi", &py_particles, &linking_length,
		       &py_boxsize3, &quota))
    return NULL;

  py_assert_ptr(comm_n_nodes() == 1);
  
  Particles* const particles=
    (Particles*) PyCapsule_GetPointer(py_particles, "_Particles");
  py_assert_ptr(particles);


  if(py_boxsize3 == Py_None)
    fof_find_groups(particles, linking_length, 0, quota);
  else {
    Float boxsize3[3];
    py_assert_ptr(PySequence_Check(py_boxsize3)); // ToDo raise error
    
    for(int k=0; k<3; ++k) {
      PyObject* const py_elem= PySequence_GetItem(py_boxsize3, k);
      py_assert_ptr(py_elem); // ToDO raise error
      py_assert_ptr(PyFloat_Check(py_elem)); // ToDo raise error
      boxsize3[k]= PyFloat_AsDouble(py_elem);
      Py_DECREF(py_elem);
    }
    fof_find_groups(particles, linking_length, boxsize3, quota);
  }



  return py_vector_asarray<Index>(fof_nfof());
}

