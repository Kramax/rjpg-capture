/* Code adapted from: https://github.com/jacksonliam/mjpg-streamer/tree/master
#                                                                              #
# This package work with the Logitech UVC based webcams with the mjpeg feature #
#                                                                              #
# Copyright (C) 2005 2006 Laurent Pinchart &&  Michel Xhaard                   #
#                    2007 Lucas van Staden                                     #
#                    2007 Tom Stöveken                                         #
#                                                                              #
# This program is free software; you can redistribute it and/or modify         #
# it under the terms of the GNU General Public License as published by         #
# the Free Software Foundation; either version 2 of the License, or            #
# (at your option) any later version.                                          #
#                                                                              #
# This program is distributed in the hope that it will be useful,              #
# but WITHOUT ANY WARRANTY; without even the implied warranty of               #
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the                #
# GNU General Public License for more details.                                 #
#                                                                              #
# You should have received a copy of the GNU General Public License            #
# along with this program; if not, write to the Free Software                  #
# Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA    #
#                                                                              #
*******************************************************************************/


#include "camera.hpp"
#include "rjpg-capture.hpp"

#include <thread>

extern "C" {
  #include <sys/types.h>
  #include <sys/stat.h>
  #include <fcntl.h>
  #include <unistd.h>  
  #include <stdlib.h>
  #include <sys/mman.h>
  #include <sys/ioctl.h>  
  #include <string.h>  
  #include <linux/types.h>          /* for videodev2.h */
  #include <linux/videodev2.h>
}

struct Camera_V4L : public Camera
{
  struct ErrorIOCTL : public std::runtime_error
  { using std::runtime_error::runtime_error; };
  struct ErrorCapture : public std::runtime_error
  { using std::runtime_error::runtime_error; };

  struct CaptureBuffer 
  {
    void *start = nullptr;
    size_t length = 0;
  };

  int _fd = -1; 
  unsigned int _width = 0;
  unsigned int _height = 0;
  static constexpr int _bufferCount = 2;
  CaptureBuffer _captureBuffers[_bufferCount];
  std::mutex _mutex;
  std::mutex _aliveMutex;
  ImageData_h _frameBuffers[2];
  int _activeFrame = 0;
  bool _alive = true;

  virtual ~Camera_V4L()
  {
    std::lock_guard<std::mutex> lock(_aliveMutex);
    _alive = false;    
  }

  /* ioctl with a number of retries in the case of failure
  * args:
  * fd - device descriptor
  * code - ioctl reference
  * arg - pointer to ioctl data
  * returns - ioctl result
  */
  int xioctl(int fd, unsigned long code, void *arg)
  {
      int ret = 0;
      int tries = 4;

      do {
          ret = ioctl(fd, code, arg);
      } while(ret && tries-- &&
              ((errno == EINTR) || (errno == EAGAIN) || (errno == ETIMEDOUT)));

      if(ret && (tries <= 0)) fprintf(stderr, "ioctl (%lu) retried - giving up: %s)\n", code, strerror(errno));

      return (ret);
  }

  template <typename S> void zero_struct(S &data)
  {
    memset(&data, 0, sizeof(data));
  }

  template <typename S> void ioctl_get(unsigned long ioctl_code, S &data, const std::string &what)
  {
    if (xioctl(_fd, ioctl_code, &data) != 0) 
    {
      LogError("ioctl_get failed: %s", what.c_str());
      throw ErrorIOCTL(string_format("ioctl_get failed for %s", what.c_str()));
    }
  }

  template <typename S> void ioctl_set(unsigned long ioctl_code, S &data, const std::string &what)
  {
    if (xioctl(_fd, ioctl_code, (void*)&data) != 0) 
    {
      LogError("ioctl_set failed: %s", what.c_str());
      throw ErrorIOCTL(string_format("ioctl_set failed for %s", what.c_str()));
    }
  }

  // some ioctl are both getting and setting
  template <typename S> void ioctl_rw(unsigned long ioctl_code, S &data, const std::string &what)
  {
    if (xioctl(_fd, ioctl_code, &data) != 0) 
    {
      LogError("ioctl_rw failed: %s", what.c_str());
      throw ErrorIOCTL(string_format("ioctl_rw failed for %s", what.c_str()));
    }
  }

  std::map<std::string,int32_t> control_ids = {
    { "auto_wb", V4L2_CID_AUTO_WHITE_BALANCE },
  };

  std::map<std::string,int32_t> ext_control_ids = {
    { "exposure_abs", V4L2_CID_EXPOSURE_ABSOLUTE },
    { "exposure_mode", V4L2_CID_EXPOSURE_AUTO }
  };

  std::map<std::string,int32_t> control_value_enums = {
    { "exposure_auto", V4L2_EXPOSURE_AUTO },
    { "exposure_manual", V4L2_EXPOSURE_MANUAL	},
    { "exposure_shutter_priority", V4L2_EXPOSURE_SHUTTER_PRIORITY	},
    { "exposure_aperature_priority", V4L2_EXPOSURE_APERTURE_PRIORITY },
  };

  void set_control(uint32_t control_id, int32_t value)
  {
    v4l2_control ctrl = {0};

    ctrl.id = control_id;
    ctrl.value = value;

    ioctl_set(VIDIOC_S_CTRL, ctrl, "set control value");
  }

  void set_extended_control(uint32_t control_id, int32_t value)
  {
    v4l2_ext_controls ext_ctrls = {0};
    v4l2_ext_control ext_ctrl = {0};

    ext_ctrl.id = control_id;
    ext_ctrl.value = value;
    ext_ctrls.count = 1;
    ext_ctrls.controls = &ext_ctrl;

    ioctl_set(VIDIOC_S_EXT_CTRLS, ext_ctrls, "set extended control value");
  }

  virtual bool set_control(const std::string &control_name, int32_t value) override
  {
    auto it = control_ids.find(control_name);

    if (it == control_ids.end())
    {
      auto jit = ext_control_ids.find(control_name);

      if (jit == ext_control_ids.end())
      {
        LogError("set_control: control %s is not available", control_name.c_str());
        return false;
      }

      try {
        set_extended_control(jit->second, value);
        return true;
      }
      catch(std::runtime_error &e) {
        LogError("set_control: extended control %s cannot be set", control_name.c_str());
        return false;
      }
    }

    try {
      set_control(it->second, value);
      return true;
    }
    catch(std::runtime_error &e) {
      LogError("set_control: control %s cannot be set", control_name.c_str());
      return false;
    }
  }

  virtual bool set_control(const std::string &control_name, const std::string &enum_value) override
  {
    auto it = control_value_enums.find(enum_value);

    if (it == control_value_enums.end())
    {
      LogError("set_control: control enum value %s is not available for control %s", 
        enum_value.c_str(), control_name.c_str());
      return false;
    }

    return set_control(control_name, it->second);
  }

  virtual void open(const std::string &path, int width, int height) override 
  {
    if (_fd != -1)
    {
      ::close(_fd);
    }

    _fd = ::open(path.c_str(), O_RDWR);

    if (_fd == -1)
    {
      throw ErrorOpen("could not open camera device");
    }

    struct v4l2_capability cap;

    ioctl_get(VIDIOC_QUERYCAP, cap, "query capabilities");

    if (cap.capabilities & V4L2_CAP_VIDEO_CAPTURE)
    {
      LogDeb("%s has capture capability", path.c_str());
    }
    else
    {
      LogError("%s does not have capture capability", path.c_str());
      throw ErrorOpen("no capture capability");
    }


    if (cap.capabilities & V4L2_CAP_STREAMING)
    {
      LogDeb("%s has streaming capability", path.c_str());
    }
    else
    {
      LogError("%s does not have streaming capability", path.c_str());
      throw ErrorOpen("no streaming capability");
    }

    LogDeb("%s name is %s", path.c_str(), cap.driver);
    LogDeb("%s card is %s", path.c_str(), cap.card);

    struct v4l2_dv_timings timings = {0};

/*
    try {
    ioctl_get(VIDIOC_QUERY_DV_TIMINGS, timings, "query video timings");
    }
    catch(std::runtime_error e) {
      LogError("Could not query video timings (%d), but continuing...", errno);
    }

    try {
    // not sure why it sets them right after getting them
    ioctl_set(VIDIOC_QUERY_DV_TIMINGS, timings, "set video timings");
    }
    catch(std::runtime_error e) {
      LogError("Could not set video timings (%d), but continuing...", errno);
    }
*/
    
    LogDeb("Got timing size %ux%u pixclk %llu\n", timings.bt.width, timings.bt.height, timings.bt.pixelclock);

    _width = width;
    _height = height;

    struct v4l2_event_subscription sub = {0};

    sub.type = V4L2_EVENT_SOURCE_CHANGE;
    try {
    ioctl_rw(VIDIOC_SUBSCRIBE_EVENT, sub, "subscribe to change events");
    }
    catch(std::runtime_error e) {
      LogError("Could not subscribe to source change event (%d), but continuing...", errno);
    }

    struct v4l2_format format = {0};

    format.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ioctl_rw(VIDIOC_G_FMT, format, "get current video format");

    if (format.type != V4L2_BUF_TYPE_VIDEO_CAPTURE)
    {
      LogError("query video format gave bad type %d", format.type);
    }
    else
    {
      LogDeb("current format: width = %u", format.fmt.pix.width);
      LogDeb("current format: height = %u", format.fmt.pix.height);
      LogDeb("current format: field = %u", format.fmt.pix.field);
      LogDeb("current format: pixelformat = %u", format.fmt.pix.pixelformat);
      LogDeb("current format: bytesperline = %u", format.fmt.pix.bytesperline);
      LogDeb("current format: sizeimage = %u", format.fmt.pix.sizeimage);
      LogDeb("current format: priv = %u", format.fmt.pix.priv);
      LogDeb("current format: flags = %u", format.fmt.pix.flags);
      LogDeb("current format: ycbcr_enc = %u", format.fmt.pix.ycbcr_enc);
      LogDeb("current format: hsv_enc = %u", format.fmt.pix.hsv_enc);
      LogDeb("current format: quantization = %u", format.fmt.pix.quantization);
      LogDeb("current format: xfer_func = %u", format.fmt.pix.xfer_func);      
    }

    format.fmt.pix.width = _width;
    format.fmt.pix.height = _height;
    format.fmt.pix.pixelformat = V4L2_PIX_FMT_MJPEG;
    format.fmt.pix.field = V4L2_FIELD_ANY;

    ioctl_rw(VIDIOC_S_FMT, format, "set video format");


    if (format.fmt.pix.width != _width || format.fmt.pix.height != _height)
    {
      _width = format.fmt.pix.width;
      _height = format.fmt.pix.height;
      LogDeb("Adjusting image size to %d x %d", _width, _height);
    }


    struct v4l2_streamparm fps_config = {0};

    fps_config.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    ioctl_rw(VIDIOC_G_PARM, fps_config, "get FPS settings");

    LogDeb("FPS timing %u/%u", fps_config.parm.capture.timeperframe.numerator, fps_config.parm.capture.timeperframe.denominator);

    v4l2_requestbuffers reqbuf_config;

    reqbuf_config.count = _bufferCount;
    reqbuf_config.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    reqbuf_config.memory = V4L2_MEMORY_MMAP;

    ioctl_set(VIDIOC_REQBUFS, reqbuf_config, "setup video buffers");

    for(int i=0; i < _bufferCount; i++)
    {
      v4l2_buffer buffer_config;

      buffer_config.index = i;
      buffer_config.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer_config.memory = V4L2_MEMORY_MMAP;

      ioctl_get(VIDIOC_QUERYBUF, buffer_config, "getting buffer config");

      _captureBuffers[i].start = 
        mmap(0 /* start anywhere */ ,
          buffer_config.length, 
          PROT_READ | PROT_WRITE, 
          MAP_SHARED, _fd,
          buffer_config.m.offset);

      if (_captureBuffers[i].start == MAP_FAILED)
      {
        LogError("mmap of capture buffer of size %d failed.", (int)buffer_config.length);
        throw ErrorOpen("mmap of capture buffer failed");
      }

      _captureBuffers[i].length = buffer_config.length;
    }

    for(int i=0; i < _bufferCount; i++)
    {
      v4l2_buffer buffer_config;

      buffer_config.index = i;
      buffer_config.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      buffer_config.memory = V4L2_MEMORY_MMAP;

      ioctl_set(VIDIOC_QBUF, buffer_config, "queue buffer");
    }

    enable_streaming(true);
  }

  void enable_streaming(bool enable_it = true)
  {
    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;

    if (enable_it)
    {
      ioctl_set(VIDIOC_STREAMON, type, "enable streaming");
    }
    else
    {
      ioctl_set(VIDIOC_STREAMOFF, type, "disable streaming");
    }
  }

// for detecting bogus JPEG frames
#define HEADERFRAME1 0xaf

  virtual void read_image_bytes(ImageData_h &data)
  {
    // enable_streaming(true);

    struct v4l2_buffer buffer_config;

    zero_struct(buffer_config);

    buffer_config.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buffer_config.memory = V4L2_MEMORY_MMAP;

    LogDeb("read_image_bytes: dequeue frame from buffer %d...", buffer_config.index);

    ioctl_rw(VIDIOC_DQBUF, buffer_config, "dequeue buffer");

    LogDeb("read_image_bytes: got frame of size %d from buffer %d", buffer_config.bytesused, buffer_config.index);

    if (buffer_config.bytesused <= HEADERFRAME1)
    {
      LogDeb("ignoring empty-ish buffer of size %d", (int)buffer_config.bytesused);
      data->clear();
      return;
    }

    if (buffer_config.index >= _bufferCount)
    {
      LogError("invalid buffer index %d", buffer_config.index);
      throw ErrorCapture("invalid buffer index");
    }

    data->resize(buffer_config.bytesused);
    memcpy(&data->front(), _captureBuffers[buffer_config.index].start, buffer_config.bytesused);

    ioctl_set(VIDIOC_QBUF, buffer_config, "requeue buffer");

    // enable_streaming(false);
  }

  virtual ImageData_h capture_frame() override
  {
    std::lock_guard<std::mutex> lock(_mutex);
    return _frameBuffers[_activeFrame];
  }

  virtual void publish_frame(ImageData_h data)
  {
    std::lock_guard<std::mutex> lock(_mutex);
    _activeFrame = !_activeFrame;
    _frameBuffers[_activeFrame] = data;
  }

  virtual void image_reader_loop() override
  {
    for( ; ; )
    {
      try {
        std::lock_guard<std::mutex> lock(_aliveMutex);

        if (!_alive)
          return;

        ImageData_h data = std::make_shared<ImageData>();
        read_image_bytes(data);

        if (data->empty())
          continue;

        publish_frame(data);
      }
      catch(std::runtime_error &e)
      {
        LogError("image_reader_loop: error grabbing frame for fd %d, continuing", _fd);
      }
    }
  }

  virtual void close() override
  {
    if (_fd != -1)
    {
      try
      {
        enable_streaming(false);
      }
      catch(const std::exception& e)
      {
        LogError("error closing down stream fd=%d", _fd);
      }
      for(int i=0; i<_bufferCount; i++)
      {
        if (_captureBuffers[i].start != nullptr)
        {
          munmap(_captureBuffers[i].start, _captureBuffers[i].length);
          _captureBuffers[i].start = nullptr;
          _captureBuffers[i].length = 0;
        }
      }
      ::close(_fd);
      _fd = -1;
    }

    std::lock_guard<std::mutex> lock(_aliveMutex);
    _alive = false;    
  }


};
