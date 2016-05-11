#include "fft.h"
#include "error.h"
#include "pm.h"
#include "py_assert.h"

static bool pm_initialised= false;

PyObject* py_pm_init(PyObject* self, PyObject* args)
{
  // pm_init(nc_pm, pm_factor, boxsize)

  int nc_pm;
  double pm_factor, boxsize;
  
  if(!PyArg_ParseTuple(args, "idd", &nc_pm, &pm_factor, &boxsize)) {
    return NULL;
  }

  size_t mem_size= fft_mem_size(nc_pm, 1);
  Mem* const mem1= new Mem("ParticleMesh", mem_size);
  Mem* const mem2= new Mem("delta_k", mem_size);

  pm_init(nc_pm, pm_factor, mem1, mem2, boxsize);

  pm_initialised= true;

  Py_RETURN_NONE;
}


PyObject* py_pm_compute_force(PyObject* self, PyObject* args)
{
  // _pm_compute_force(_particles)
  if(!pm_initialised) {
    PyErr_SetString(PyExc_RuntimeError, "PM not initialised; call pm_init().");
    return NULL;
  }
  
  PyObject* py_particles;
  
  if(!PyArg_ParseTuple(args, "O", &py_particles))
    return NULL;

  Particles* const particles=
    (Particles *) PyCapsule_GetPointer(py_particles, "_Particles");
  py_assert_ptr(particles);

  try {
    pm_compute_force(particles);
  }
  catch(const RuntimeError e) {
    PyErr_SetNone(PyExc_RuntimeError);
    return NULL;
  }

  Py_RETURN_NONE;
}


