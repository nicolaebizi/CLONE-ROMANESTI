#!/bin/bash

# Test build for Plus3 firmware to make sure everything works

echo "=== Testing final Plus3 build setup ==="

cd /Users/nicolaemuntean/Documents/proiecte_soft/fruit-bat/pico-zxspectrum

# Create output directory
mkdir -p uf2_rp2040_plus3

# Run specific cmake build targeting Plus3 with board definition 
echo "Configuring CMake for Plus3 build..."
cmake -DPICO_COPY_TO_RAM=0 -DPICO_PLATFORM=rp2040 -DPICO_BOARD=pico_plus3 -Bfinal_build_test -H. > cmake_config.log 2>&1

if [ $? -eq 0 ]; then
    echo "CMake configuration successful!"
    
    # Check if we can create minimal build without HDMI dependencies
    cd final_build_test
    
    # Try building just core library targets to see what works
    echo "Attempting to build core components..."
    make -j2 > make_core.log 2>&1 &
    
    sleep 5
    
    # Check if any UF2 files were generated
    find . -name "*.uf2" -type f | head -5
    cd ..
else
    echo "CMake configuration failed!"
    cat cmake_config.log
fi

echo "=== Test completed ==="