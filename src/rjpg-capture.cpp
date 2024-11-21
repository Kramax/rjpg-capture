
#include <vector>
#include <cstdio>
#include <string>
#include <mutex>
#include <string>
#include <source_location>
#include <stdio.h>
#include <memory>
#include <chrono>
#include <iostream>

#include "httpd.hpp"
#include "rjpg-capture.hpp"
#include "camera_dummy.hpp"
#include "camera_v4l.hpp"
#include "argparse.hpp"

bool verbose_debug = false;

#ifdef HAS_SOURCE_LOCATION // sadly rpi debian is not up to date and is missing this
void LogErrorImpl(const std::source_location location, const std::string &msg)
{
  fprintf(stderr, "%s:%u %s", location.file_name(), location.line(), msg.c_str());

  if (msg.back() != '\n')
    fputc('\n', stderr);
}

void LogDebImp(const std::source_location location, const std::string &msg)
{
  LogErrorImpl(location, msg); // for now
}

void ReportErrorImpl(const std::source_location location, const std::string &msg)
{
  LogErrorImpl(location, msg);
}
#else
void LogErrorImpl(const std::string &msg)
{
  auto d = std::chrono::system_clock::now().time_since_epoch();
  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(d);
  std::cerr << duration;

  fprintf(stderr, ": %s", msg.c_str());

  if (msg.back() != '\n')
    fputc('\n', stderr);
}

void LogDebImp(const std::string &msg)
{
  LogErrorImpl(msg); // for now
}

void ReportErrorImpl(const std::string &msg)
{
  LogErrorImpl(msg);
}
#endif


struct CustomArgs : public argparse::Args {
    std::string &src_path  = kwarg("d,device", "camera device path").set_default("dummy");
    int &port              = kwarg("p,port", "port to bind to").set_default(8080);
    int &width             = kwarg("w,width", "desired frame width").set_default(1280);
    int &height            = kwarg("h,height", "desired frame height").set_default(720);
    int &exposure          = kwarg("e,exposure", "exposure integer").set_default(0);
    bool &background       = flag("b,daemon", "background as a daemon");
    bool &dummy_cam        = flag("D,dummy", "use a dummy camera");
    bool &verbose          = flag("v,verbose", "verbose mode");
    // float &alpha           = kwarg("a,alpha", "An optional float value").set_default(0.5f);
};

int main(int argc, char* argv[])
{
  auto args = argparse::parse<CustomArgs>(argc, argv);

  verbose_debug = args.verbose;

  if (args.background)
  {
    if (daemon(1, 0) != 0)
    {
      LogError("Could not fork to background: %d", errno);
      return 1;
    }
  }

  using namespace httplib;

  std::unique_ptr<Camera> camera;

  if (args.dummy_cam)
  {
    camera.reset(new CameraDummy);
  }
  else
  {
    if (args.src_path == "dummy")
    {
      std::cerr << "please specify a device file with -d or --device\n";
      return 1;
    }
    camera.reset(new Camera_V4L);
  }

  try
  {
    camera->open(args.src_path, args.width, args.height);
  }
  catch(const std::exception& e)
  {
    LogError("Could not open camera: %s", e.what());
    return 1;
  }
  
  if (args.exposure > 0)
  {
    camera->set_control("exposure_mode", "exposure_manual");
    camera->set_control("exposure_abs", args.exposure);
  }

  httplib::Server svr;

  camera->run_reader();

  svr.Get("/capture-image", [&camera](const Request& req, Response& res) {
    if (req.has_header("Content-Length")) {
      auto val = req.get_header_value("Content-Length");
    }
    if (req.has_param("key")) {
      auto val = req.get_param_value("key");
    }


    try {
      Camera::ImageData_h data = camera->capture_frame();

      if (data->empty())
      {
        throw std::runtime_error("no frame data");
      }

      res.set_content(&data->front(), data->size(), "image/jpeg");
    }
    catch(std::exception &e) {
      LogError("could not read image data: %s\n", e.what());
      throw e;
    }
  });


  svr.listen("0.0.0.0", args.port);

  camera->close();
  
  return 0;
}