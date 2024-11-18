#ifndef _CAMERA_HPP
#define _CAMERA_HPP

#include <vector>
#include <string>
#include <cstdio>
#include <stdexcept>

struct Camera {
  struct ErrorOpen : public std::runtime_error
  { using std::runtime_error::runtime_error; };

  virtual ~Camera()
  {
  }

  virtual void open(const std::string &path) = 0;
  virtual void read_image_bytes(std::vector<char> &data) = 0;
  virtual void close() = 0;
};

#endif