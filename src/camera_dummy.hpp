#include "camera.hpp"
#include "rjpg-capture.hpp"

struct CameraDummy : public Camera
{
  struct file_read_exception : public std::runtime_error
  {
    using std::runtime_error::runtime_error;
  };

  int image_count = 0;

  static void slurp_file(std::string path, std::vector<char> &result) 
  {
    auto fp = std::fopen(path.c_str(), "rb");

    if (fp == nullptr)
      throw file_read_exception("could not open " + path);
    
    if (std::fseek(fp, 0u, SEEK_END) != 0) 
      throw file_read_exception("could not seek to end of " + path);

    auto size = std::ftell(fp);

    if (size < 0)
      throw file_read_exception("could not get size of " + path);

    result.resize(size);

    if (std::fseek(fp, 0u, SEEK_SET) != 0) 
      throw file_read_exception("could not seek to beginning of " + path);

    if (std::fread(&result.front(), 1u, size, fp) != size)
      throw file_read_exception("could not read contents of " + path);

    std::fclose(fp);
  }

  virtual void open(const std::string &path, int width, int height) override
  {

  }

  virtual void image_reader_loop() override
  {
  }

  virtual ImageData_h capture_frame() override
  {
    try {
      auto filename = string_format("test-images/test-image-%d.jpg", image_count++ % 10);

      ImageData_h contents = std::make_shared<ImageData>();

      slurp_file(filename.c_str(), *contents);

      return contents;
    }
    catch(file_read_exception e) {
      fprintf(stderr, "could not read file: %s\n", e.what());
      throw e;
    }
  }
  
  virtual void close() override
  {

  }
};