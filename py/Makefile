# make
#   to compile this python module
# make install
#   to install it to a python module directory

all: build install


SRC  := py_package.cpp py_msg.cpp

build: $(SRC)
	python3 setup.py config build_ext --inplace

install:
	python3 setup.py install


.PHONY: clean

clean:
	rm -rf build fs/*.so
