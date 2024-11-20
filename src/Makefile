CPPARGS=-fcolor-diagnostics -std=c++20

rjpg-capture:	rjpg-capture.cpp httpd.hpp rjpg-capture.hpp camera.hpp camera_v4l.hpp
	clang++ ${CPPARGS} -o rjpg-capture rjpg-capture.cpp

all::	rjpg-capture

cross::
	~/x-tools/armv8-rpi3-linux-gnueabihf/bin/armv8-rpi3-linux-gnueabihf-g++ -std=c++20 -o rjpg-capture.arm rjpg-capture.cpp -lpthread
	# ~/x-tools/aarch64-rpi3-linux-gnu/bin/aarch64-rpi3-linux-gnu-g++ -std=c++20 -o rjpg-capture.arm rjpg-capture.cpp -lpthread
