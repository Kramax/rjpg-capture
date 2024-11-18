CPPARGS=-fcolor-diagnostics -std=c++20

rjpg-capture:	rjpg-capture.cpp httpd.hpp rjpg-capture.hpp camera.hpp camera_v4l.hpp
	clang++ ${CPPARGS} -o rjpg-capture rjpg-capture.cpp

all::	rjpg-capture
