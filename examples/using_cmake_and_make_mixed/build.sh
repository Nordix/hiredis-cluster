#!/bin/sh
set -e

# This script builds and installs hiredis using GNU Make and hiredis-cluster using CMake.
# The shared library variants are used when building the examples.

script_dir=$(realpath "${0%/*}")
repo_dir=$(git rev-parse --show-toplevel)

# Download hiredis
hiredis_version=1.2.0
curl -L https://github.com/redis/hiredis/archive/v${hiredis_version}.tar.gz | tar -xz -C ${script_dir}

# Build and install downloaded hiredis using GNU Make
make -C ${script_dir}/hiredis-${hiredis_version} \
     USE_SSL=1 \
     DESTDIR=${script_dir}/install \
     all install


# Build and install hiredis-cluster from the repo using CMake.
mkdir -p ${script_dir}/hiredis_cluster_build
cd ${script_dir}/hiredis_cluster_build
cmake -DCMAKE_BUILD_TYPE=RelWithDebInfo -DDISABLE_TESTS=ON -DENABLE_SSL=ON -DDOWNLOAD_HIREDIS=OFF \
      -DCMAKE_PREFIX_PATH=${script_dir}/install/usr/local \
      ${repo_dir}
make DESTDIR=${script_dir}/install clean install


# Build example binaries by providing shared libraries
make -C ${repo_dir} CFLAGS="-I${script_dir}/install/usr/local/include" \
     LDFLAGS="-lhiredis_cluster -lhiredis_cluster_ssl -lhiredis -lhiredis_ssl \
              -L${script_dir}/install/usr/local/lib/ \
              -Wl,-rpath=${script_dir}/install/usr/local/lib/" \
     USE_SSL=1 \
     clean examples
