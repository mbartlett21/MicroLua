export PICO_SDK_PATH=~/pico-sdk

cmake -B build -DPICO_BOARD=pico_w
make -C build
