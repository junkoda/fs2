#include <vector>
#include <cstdio>
#include <cmath>
#include <cassert>
#include <mpi.h>

#include <gsl/gsl_integration.h>
#include <gsl/gsl_roots.h>
#include <gsl/gsl_sf_hyperg.h> 
#include <gsl/gsl_errno.h>

#include "particle.h"
#include "msg.h"
#include "cola.h"
#include "cosmology.h"

using namespace std;

namespace {
  constexpr double nLPT= -2.5f;

  double Sq(double ai, double af, double aRef);
}

void cola_set_initial(Particles* const particles, const double a)
{
  Particle* const p= particles->p;
  const int np= particles->np_local;

  const Float da1= cosmology_D_growth(a);
  const Float da2= cosmology_D2_growth(a, da1);
    
  const Float Dv= cosmology_Dv_growth(a, da1);
  const Float D2v= cosmology_D2v_growth(a, da2);

  for(int i=0; i<np; i++) {
    p[i].v[0]= p[i].dx1[0]*Dv + p[i].dx2[0]*D2v;
    p[i].v[1]= p[i].dx1[1]*Dv + p[i].dx2[1]*D2v;
    p[i].v[2]= p[i].dx1[2]*Dv + p[i].dx2[2]*D2v;
  }

  particles->a_v= a;
  
  msg_printf(msg_info, "Leapfrog (non-cola) initial velocity set at a= %.3f\n", a);
  msg_printf(msg_debug, "Dv= %e, Dv2= %e\n", Dv, D2v);
}

void cola_kick(Particles* const particles, const double avel1)
{
  const double ai=  particles->a_v;  // t - 0.5*dt
  const double a=   particles->a_x;  // t
  const double af=  avel1;           // t + 0.5*dt

  const double om= cosmology_omega_m();
  msg_printf(msg_info, "Kick %lg -> %lg\n", ai, avel1);

  const Float kick_factor= (pow(af, nLPT) - pow(ai, nLPT))/
                             (nLPT*pow(a, nLPT)*sqrt(om/a + (1.0 - om)*a*a));
  
  const double growth1= cosmology_D_growth(a);
  const double growth2= cosmology_D2_growth(a, growth1);
	
  msg_printf(msg_debug, "growth factor %lg\n", growth1);

  const Float q1= growth1;
  const Float q2= cosmology_D2a_growth(growth1, growth2);

  
  Particle* const p= particles->p;
  const size_t np= particles->np_local;
  Float3* const f= particles->force;

  
  // Kick using acceleration at scale factor a
  // Assume forces at 'a' is in particles->force
#ifdef _OPENMP
  #pragma omp parallel for default(shared)
#endif
  for(size_t i=0; i<np; i++) {
    Float ax= -1.5*om*(f[i][0] + p[i].dx1[0]*q1 + p[i].dx2[0]*q2);
    Float ay= -1.5*om*(f[i][1] + p[i].dx1[1]*q1 + p[i].dx2[1]*q2);
    Float az= -1.5*om*(f[i][2] + p[i].dx1[2]*q1 + p[i].dx2[2]*q2);

    p[i].v[0] += ax*kick_factor;
    p[i].v[1] += ay*kick_factor;
    p[i].v[2] += az*kick_factor;

  }

  // velocity is now at a= avel1
  particles->a_v= avel1;
}

void cola_drift(Particles* const particles, const double apos1)
{
  const double ai= particles->a_x;
  const double af= apos1;
  
  Particle* const p= particles->p;
  const size_t np= particles->np_local;

  const double dt=Sq(ai, af, particles->a_v);

  const double growth_i= cosmology_D_growth(ai);
  const double growth_f= cosmology_D_growth(af);
  const Float da1= growth_f - growth_i;

  const Float da2= cosmology_D2_growth(af, growth_f) -
                     cosmology_D2_growth(ai, growth_i);

  msg_printf(msg_info, "Drift %lg -> %lg\n", ai, af);
    
  // Drift
#ifdef _OPENMP
  #pragma omp parallel for default(shared)
#endif
  for(size_t i=0; i<np; i++) {
    p[i].x[0] += p[i].v[0]*dt + 
                 (p[i].dx1[0]*da1 + p[i].dx2[0]*da2);
    p[i].x[1] += p[i].v[1]*dt +
                 (p[i].dx1[1]*da1 + p[i].dx2[1]*da2);
    p[i].x[2] += p[i].v[2]*dt + 
                 (p[i].dx1[2]*da1 + p[i].dx2[2]*da2);
  }

  particles->a_x= af;
}

vector<Float> cola_velocity(Particles const * const particles)
{
  // Convert COLA 2LPT subtracted velocity to usual velocity
  const size_t np= particles->np_local;
  vector<Float> v(3*np);

  const double a= particles->a_x;
  const double D1= cosmology_D_growth(a);
  const Float  Dv= cosmology_Dv_growth(a, D1);
  const Float  D2v= cosmology_D2v_growth(a, D1);

  Particle const * const p= particles->p;  
  for(size_t i=0; i<np; ++i) {
    for(size_t k=0; k<3; ++k) {
      v[3*i + k]= p[i].v[k] + Dv*p[i].dx1[k] + D2v*p[i].dx2[k];
    }
  }

  return v;
}

namespace {
  
double fun (double a, void * params) {
  double om= *(double*)params;
  return pow(a, nLPT)/(sqrt(om/(a*a*a) + 1.0 - om)*a*a*a);
}

double Sq(double ai, double af, double av) {
  //
  // \int (a(t)/a(av))^nLPT dt/a(t)^2
  // = \int_ai^af (a/a(av))^nLPT da/(a^3 H(a))
  //
  assert(ai > 0.0);
  const int n_workspace= 5000;
  gsl_integration_workspace* w= gsl_integration_workspace_alloc(n_workspace);
  
  double result, error;
  double omega_m= cosmology_omega_m();
  
  gsl_function F;
  F.function= &fun;
  F.params= &omega_m;
  
  gsl_integration_qag(&F, ai, af, 0, 1e-5, n_workspace, 6,
		      w, &result, &error); 
  
  gsl_integration_workspace_free(w);
     
  return result/pow(av, nLPT);
}
  
} // unnamed namespace
