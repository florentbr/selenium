// Licensed to the Software Freedom Conservancy (SFC) under one
// or more contributor license agreements. See the NOTICE file
// distributed with this work for additional information
// regarding copyright ownership. The SFC licenses this file
// to you under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <logging.h>
#include <atlimage.h>
#include <atlenc.h>

namespace webdriver {


/// <summary>
/// Get a native window size.
/// Returns true if succeed, false otherwise.
/// </summary>
bool GetWindowSize(HWND hwnd, int* width, int* height) {
  RECT rect;
  if (!::GetWindowRect(hwnd, &rect))
    return false;

  *width = rect.right - rect.left;
  *height = rect.bottom - rect.top;
  return true;
}


/// <summary>
/// Resize a native window without sending the WM_WINDOWPOSCHANGING message.
/// Returns true if succeed, false otherwise.
/// </summary>
bool SetWindowSize(HWND hwnd, int width, int height) {
  const UINT uFlags = SWP_NOSENDCHANGING | SWP_NOMOVE
    | SWP_NOOWNERZORDER | SWP_NOZORDER | SWP_NOACTIVATE;

  BOOL succeed = ::SetWindowPos(hwnd, NULL, 0, 0, width, height, uFlags);

  if (succeed) {
    int width2(0), height2(0);
    GetWindowSize(hwnd, &width2, &height2);
    succeed = ((width2 == width) && (height2 == height));
  }

  if (!succeed) {
    LOG(WARN) << "Failed to resize the window to w=" << width << " h=" << height;
  }
  return succeed;
}


/// <summary>
/// Returns true if all the pixels within check_width and check_height are
/// identical, false otherwise. The input CImage must to be 32bits per pixel.
/// </summary>
bool IsImageSameColour(CImage* image, int check_width, int check_height) {
  if (image->GetBPP() != 32)
    throw new std::invalid_argument("Invalid image bit depth. Must be 32 BPP.");

  int width = min(check_width, image->GetWidth());
  int height = min(check_height, image->GetHeight());
  int row_stride = image->GetPitch();
  BYTE* row_ptr = (BYTE*)image->GetBits();
  INT32 first_pixel = ((INT32*)row_ptr)[0];

  for (int y = 0; y < height; y++) {
    INT32* ptr = (INT32*)row_ptr;
    for (int x = 0; x < width; x++) {
      if (first_pixel != ptr[x])
        return false;
    }
    row_ptr += row_stride;
  }

  return true;
}


/// <summary>
/// Converts a CImage to a PNG base64 string.
/// </summary>
HRESULT ConvImageToPngBase64string(CImage* image, std::string& data) {
  LOG(TRACE) << "Entering ConvImageToBase64string";

  if (image == NULL) {
    LOG(DEBUG) << "CImage was not initialized.";
    return E_POINTER;
  }

  CComPtr<IStream> stream;
  HRESULT hr = ::CreateStreamOnHGlobal(NULL, TRUE, &stream);
  if (FAILED(hr)) {
    LOGHR(WARN, hr) << "Error is occured during creating IStream";
    return hr;
  }

  GUID image_format = Gdiplus::ImageFormatPNG /*Gdiplus::ImageFormatJPEG*/;
  hr = image->Save(stream, image_format);
  if (FAILED(hr)) {
    LOGHR(WARN, hr) << "Saving screenshot image is failed";
    return hr;
  }

  // Get the size of the stream.
  STATSTG statstg;
  hr = stream->Stat(&statstg, STATFLAG_DEFAULT);
  if (FAILED(hr)) {
    LOGHR(WARN, hr) << "No stat on stream is got";
    return hr;
  }

  HGLOBAL global_memory_handle = NULL;
  hr = ::GetHGlobalFromStream(stream, &global_memory_handle);
  if (FAILED(hr)) {
    LOGHR(WARN, hr) << "No HGlobal in stream";
    return hr;
  }

  // TODO: What if the file is bigger than max_int?
  int stream_size = static_cast<int>(statstg.cbSize.QuadPart);
  LOG(DEBUG) << "Size of screenshot image stream is " << stream_size;

  int length = ::Base64EncodeGetRequiredLength(stream_size, ATL_BASE64_FLAG_NOCRLF);
  if (length <= 0) {
    LOG(WARN) << "Got zero or negative length from base64 required length";
    return E_FAIL;
  }

  BYTE* global_lock = reinterpret_cast<BYTE*>(::GlobalLock(global_memory_handle));
  if (global_lock == NULL) {
    LOGERR(WARN) << "Unable to lock memory for base64 encoding";
    ::GlobalUnlock(global_memory_handle);
    return E_FAIL;
  }

  char* data_array = new char[length + 1];
  if (!::Base64Encode(global_lock,
    stream_size,
    data_array,
    &length,
    ATL_BASE64_FLAG_NOCRLF)) {
    delete[] data_array;
    ::GlobalUnlock(global_memory_handle);
    LOG(WARN) << "Unable to encode image stream to base64";
    return E_FAIL;
  }
  data_array[length] = '\0';
  data = data_array;

  delete[] data_array;
  ::GlobalUnlock(global_memory_handle);

  return S_OK;
}


} // namespace webdriver