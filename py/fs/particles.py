import fs._fs as c


class Particles(object):
    """
    A set of particles.
    Use lpt() to create a set of particles.

    Attributes:
        x (np.array [float]): positions (np_local, 3)
        id (np.array [uint64]):
        force (np.array [float]):
        dx1
        dx2
        fof_group
        np_total [unsigned long]: total number of particles

    """
    def __init__(self, np=10, boxsize=0.0, *, _particles=None):
        # Particles(nc, boxsize) or Particles(_particles=particles)
        if _particles is None:
            self._particles = c._particles_alloc(np, boxsize)
        else:            
            self._particles = _particles


    def __getitem__(self, index):
        """
        a = particles.__getitem__(i:j:k) <==> particles[i:j:k]

        Returns:
            local particles as a np.array.

        * particles[i]:     ith particles.
        * particles[i:j]:   particles with indeces [i, j).
        * particles[i:j:k]: particles with indeces i + k*n smaller than j.


        Args:
           index: an integer or a slice i:j:k

        Returns:
            particle data in internal units as np.array.

            * a[0:3]:  positions.
            * a[3:6]:  velocities (internal unit).
            * a[6:9]:  1st-order LPT displacements.
            * a[9:12]: 2nd-order LPT displacements.
        """
        return c._particles_getitem(self._particles, i)

    def __len__(self):
        """
        len(particles)

        Returns:
            local number of particles in this MPI node.
        """
        return c._particles_len(self._particles)

    def resize(self, np_local):
        """Set local number of particles to np_local"""
        c._particles_reseize(self._particles, np_local)
        
    def update_np_total(self):
        c._particles_update_np_total(self._particles)

    def append(self, x):
        """Append particles with positions x.
        Args:
           x: np.array with 3 columns for x,y,z
              x can be None, resulting in no change in particles
        """
        c._particles_append(self._particles, x)

    def clear(self):
        """Remove all particles"""
        c._particles_clear(self._particles)

    def slice(self, frac):
        return c._particles_slice(self._particles, frac)

    def save_gadget_binary(self, filename, use_longid=False):
        """
        Args:
            filename (str): Output file name.
            use_longid (bool): Write 8-byte ID (default: False)
        """
        c._write_gadget_binary(self._particles, filename, use_longid)

    def save_hdf5(self, filename, var):
        """
        Args:
            filename (str): Output file name.
            var (str): Output variables, a subset of 'ixvf12' in this order.

                * i: ID
                * x: positions
                * v: velocities
                * f: force
                * 1: 1LPT displacements (a=1)
                * 2: 2nd-order displacements (a=1)
        """
        c._hdf5_write_particles(self._particles, filename, var)

    def periodic_wrapup(self):
        """Periodic wrap up particle poisitions
        """
        c._particles_periodic_wrapup(self._particles)

    @property
    def np_local(self):
        return c._particles_len(self._particles)

    @property
    def np_total(self):
        return c._particles_np_total(self._particles)

    @property
    def id(self):
        return c._particles_id_asarray(self._particles)

    @property
    def x(self):
        return c._particles_x_asarray(self._particles, 0)

    @property
    def dx1(self):
        return c._particles_x_asarray(self._particles, 1)

    @property
    def dx2(self):
        return c._particles_x_asarray(self._particles, 2)


    @property
    def force(self):
        return c._particles_force_asarray(self._particles)

    @property
    def fof_group(self):
        """fof_group[i] is the group index that particle i belongs to.
           i (0 <= i < np_local)
        """
        return c._fof_grp()
