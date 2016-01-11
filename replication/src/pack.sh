#1/bin/bash

usage() {
    echo "need env: \$TOOLCHAIN_DIR, \$GLIBC_DIR, \$BOOST_DIR"
    echo "usage: ./pack.sh version"
    echo "    this command will create pegasus-<version>.tar.gz file under current folder for deploy."
}


if [ "$#" -lt 1 ]; then
    echo "should pass version to this script"
    usage
    exit 1
fi

if [ -z "$TOOLCHAIN_DIR" ]; then
    echo "build machine needs env var \$TOOLCHAIN_DIR"
    usage
    exit 1
fi

if [ -z "$GLIBC_DIR" ]; then
    echo "build machine need env var \$GLIBC_DIR"
    usage
    exit 1
fi

if [ -z "$BOOST_DIR" ]; then
    echo "build machine needs env var \$BOOST_DIR"
    usage
    exit 1
fi

version=$1
sh ./build.sh

# create pack folder
pack_folder="pegasus-""$version"
if [ -d "$pack_folder" ]; then
  rm -rf "$pack_folder"
fi
mkdir "$pack_folder"

# copy pegasus binaries
mkdir "$pack_folder""/bin"
cp builder/bin/rrdb/rrdb "$pack_folder""/bin"

# copy tool chain
mkdir "$pack_folder""/toolchain"
tool_chain_folder="$(dirname "$TOOLCHAIN_DIR")"
cp -r "$tool_chain_folder" "$pack_folder""/toolchain"

# copy glibc
cp -r "$GLIBC_DIR" "$pack_folder""/toolchain"

# copy boost lib
mkdir "$pack_folder""/core"
boost_dir="$BOOST_DIR""/lib"
cp "$boost_dir"/* "$pack_folder""/core"

# copy dsn core lib
cp "../../../rDSN/install/lib/libdsn.core.so" "$pack_folder""/core"

# copy shm counter binary
mkdir "$pack_folder""/shmcounter"
cp -r ../ext/shmcounter/bin/* "$pack_folder""/shmcounter"

pack_file="pegasus-""$version"".tar.gz"

tar -czf "$pack_file" "$pack_folder"

