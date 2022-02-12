#!/bin/bash

#EN: openmpi with threads support installed into /opt/openmpi
#PL: openmpi ze wsparciem dla wątków instalowane do katalogu /opt/openmpi

wget http://www.open-mpi.org/software/ompi/v1.4/downloads/openmpi-1.4.5.tar.gz
tar xzf openmpi-1.4.5.tar.gz
cd openmpi-1.4.5/
mkdir build
cd build
../configure --prefix=/opt/openmpi --enable-mpirun-prefix-by-default --with-threads=posix --enable-mpi-threads
sudo make all install

