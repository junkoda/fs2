#include "cosmology.h"
#include "particle.h"
#include "fft.h"
#include "lpt.h"
#include "py_assert.h"
#include "py_particles.h"
#include "py_lpt.h"

PyObject* py_lpt(PyObject* self, PyObject* args)
{
  //_lpt(nc, boxsize, a, _ps, seed

  PyObject* py_ps;
  int nc;
  double a, boxsize;
  unsigned long seed;
  char const* kind;

  if(!PyArg_ParseTuple(args, "iddkOs", &nc, &boxsize, &a,  &seed, &py_ps, &kind)) {
    return NULL;
  }

  PowerSpectrum* const ps=
    (PowerSpectrum *) PyCapsule_GetPointer(py_ps, "_PowerSpectrum");
  py_assert_ptr(ps);

  size_t nx= fft_local_nx(nc);
  size_t np_alloc= (size_t)((1.25*(nx + 1)*nc*nc));

  Particles* particles= new Particles(np_alloc, boxsize);
    
  size_t mem_size= 9*fft_mem_size(nc, 0);
  Mem* const mem= new Mem("LPT", mem_size);

  lpt_init(nc, boxsize, mem);
  lpt_set_displacements(seed, ps, a, kind, particles);


  delete mem;
  
  return PyCapsule_New(particles, "_Particles", py_particles_free);  
}

PyObject* py_lpt_set_offset(PyObject* self, PyObject* args)
{
  double offset;
  
  if(!PyArg_ParseTuple(args, "d", &offset)) {
    return NULL;
  }
  
  lpt_set_offset(offset);

  Py_RETURN_NONE;
}

PyObject* py_lpt_set_zeldovich_force(PyObject* self, PyObject* args)
{
  // _lpt_set_zeldovich_force(_particles, a)
  PyObject *py_particles;
  double a;
  
  if(!PyArg_ParseTuple(args, "Od", &py_particles, &a)) {
    return NULL;
  }

  Particles* const particles=
    (Particles *) PyCapsule_GetPointer(py_particles, "_Particles");
  py_assert_ptr(particles);

  const Float growth1= cosmology_D_growth(a);

  const size_t n= particles->np_local;
  Float3 * const f= particles->force;
  Particle const * const p= particles->p;
  
  for(size_t i=0; i<n; ++i) {
    for(int k=0; k<3; ++k)
      f[i][k]= growth1*p[i].dx1[k];
  }

  Py_RETURN_NONE;
}
