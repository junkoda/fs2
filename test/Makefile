TESTS := test_fft test_pm_cic test_pm_density test_particles_h5 
TESTS += test_pm_force
TESTS += test_cola


# $(basename names...)

test: $(TESTS)

%: %.py
	for n in 1 2 3 4; do mpirun -n $$n python3 $< || exit 1; done

force_double.h5: create_force_h5.py
	python3 $<

pm_density_double.h5: create_pm_density_h5.py
	python3 $<

test_pm_force: test_pm_force.py force_double.h5
test_pm_density: test_pm_density.py pm_density_double.h5

.PHONY: check

check:
	pep8 .

