#!/bin/bash

mkdir -p Debug
mpicc src/PR.c -o Debug/PR -lrt

