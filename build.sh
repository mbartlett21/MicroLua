# required for correct version of bashrc.
# adds /opt/cmake-3.30.2-linux-x86_64/bin to the PATH
. ~/.bashrc

export PICO_SDK_PATH=~/pico-sdk
# export PATH="/opt/cmake-3.30.2-linux-x86_64/bin:$PATH"

cmake -B build -DPICO_BOARD=pico_w
make -C build -j9
