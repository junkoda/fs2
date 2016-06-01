#
# Compare PM force at particle locations
# Test parallelisation does not change force
#

import fs
import numpy as np
import h5py
import pm_setup

# read reference file
file = h5py.File('force_%s.h5' % fs.config_precision(), 'r')

ref_id = file['id'][:]
ref_force = file['f'][:]

file.close()

#print(ref_id)
#print(ref_force)

fs.set_loglevel(0)

particles = pm_setup.force()
particle_id = particles.id
particle_force = particles.force

if fs.comm_this_node() == 0:
    assert(np.all(particle_id == ref_id))
    print('pm_force id OK')

    force_rms = np.std(ref_force)

    diff = particle_force - ref_force

    diff_rms = np.std(diff)
    print('pm_force rms error %e / %e' % (diff_rms, force_rms))

    diff_max = np.max(np.abs(diff))
    print('pm_force max error %e / %e' % (diff_max, force_rms))

    assert(diff_max < 10.0e-15)

fs.comm_mpi_finalise()