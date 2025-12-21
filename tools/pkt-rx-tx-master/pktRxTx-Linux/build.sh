dir="build/$(uname -s)-$(uname -m)"
mkdir -p "$dir"
cmake -B$dir
cmake --build $dir
