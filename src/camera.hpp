#ifndef _CAMERA_HPP
#define _CAMERA_HPP

#include <vector>
#include <string>
#include <cstdio>
#include <stdexcept>
#include <thread>
#include <memory>

struct Camera {
  struct ErrorOpen : public std::runtime_error
  { using std::runtime_error::runtime_error; };

  typedef std::vector<char> ImageData;
  typedef std::shared_ptr<ImageData> ImageData_h;

  std::thread _readerThread;

  virtual ~Camera()
  {
  }

  virtual void open(const std::string &path, int width, int height) = 0;
  virtual ImageData_h capture_frame() = 0;
  virtual bool set_control(const std::string &control_name, int32_t value) { return false; }
  virtual bool set_control(const std::string &control_name, const std::string &enum_value) { return false; }
  virtual void image_reader_loop() = 0;

  virtual void run_reader()
  {
    _readerThread = std::thread([this] {
      image_reader_loop();
    });
  }

  virtual void close() = 0;
};

#endif