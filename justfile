build_dir := "build"
bench_dir := "build-bench"

# list recipes
default:
    @just --list

# generate the build system
configure:
    cmake -S . -B {{build_dir}}

# compile
build: configure
    cmake --build {{build_dir}} --parallel {{num_cpus()}}

# optimized build without sanitizers, for measuring
build-bench:
    cmake -S . -B {{bench_dir}} -DCMAKE_BUILD_TYPE=Release -DSPEEDTEST_SANITIZE=OFF
    cmake --build {{bench_dir}} --parallel {{num_cpus()}}

# build and run, passing any args through
run *args: build
    ./{{build_dir}}/speedtest {{args}}

# wipe the build directory
clean:
    rm -rf {{build_dir}} {{bench_dir}}

# clean, then build
rebuild: clean build
