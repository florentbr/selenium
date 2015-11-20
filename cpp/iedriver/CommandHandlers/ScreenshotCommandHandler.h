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

#ifndef WEBDRIVER_IE_SCREENSHOTCOMMANDHANDLER_H_
#define WEBDRIVER_IE_SCREENSHOTCOMMANDHANDLER_H_

#include "../Browser.h"
#include "../IECommandHandler.h"
#include "../IECommandExecutor.h"
#include "../ScreenshotUtilities.h"
#include "logging.h"
#include <atlimage.h>
#include <atlenc.h>

namespace webdriver {

class ScreenshotCommandHandler : public IECommandHandler {
 public:
  ScreenshotCommandHandler(void) {
    this->image_ = NULL;
  }

  virtual ~ScreenshotCommandHandler(void) {
  }

 protected:
  void ExecuteInternal(const IECommandExecutor& executor,
                       const ParametersMap& command_parameters,
                       Response* response) {
    LOG(TRACE) << "Entering ScreenshotCommandHandler::ExecuteInternal";

    BrowserHandle browser_wrapper;
    int status_code = executor.GetCurrentBrowser(&browser_wrapper);
    if (status_code != WD_SUCCESS) {
      response->SetErrorResponse(status_code, "Unable to get browser");
      return;
    }

    HRESULT hr;

    // Capture the view
    int tries = 2;
    for (int i = 1; ; i++) {
      hr = this->CaptureBrowser(browser_wrapper);
      if (SUCCEEDED(hr))
        break;

      LOGHR(WARN, hr) << "Failed to capture browser image at " << i << " try";
      this->ClearImage();

      if (i >= tries) {
        response->SetSuccessResponse("");
        return;
      }
    }

    // Convert to a base64 string
    std::string base64_screenshot = "";
    hr = ConvImageToPngBase64string(this->image_, base64_screenshot);
    if (FAILED(hr)) {
      LOGHR(WARN, hr) << "Unable to transform browser image to Base64 format";
      this->ClearImage();
      response->SetSuccessResponse("");
      return;
    }

    this->ClearImage();
    response->SetSuccessResponse(base64_screenshot);
  }

 private:
  ATL::CImage* image_;

  void ClearImage() {
    if (this->image_ != NULL) {
      delete this->image_;
      this->image_ = NULL;
    }
  }

  HRESULT CaptureBrowser(BrowserHandle browser) {
    LOG(TRACE) << "Entering ScreenshotCommandHandler::CaptureBrowser";

    // Get ie window and content view handles
    HWND ie_window_handle = browser->GetTopLevelWindowHandle();
    HWND content_window_handle = browser->GetContentWindowHandle();
    if (ie_window_handle == 0 || content_window_handle == 0)
      return E_FAIL;

    // Get document IHTMLDocument2 and IHTMLDocument3 interfaces
    CComPtr<IHTMLDocument2> document2;
    CComPtr<IHTMLDocument3> document3;
    if (!GetDocument(browser, &document2, &document3))
      return E_FAIL;

    // Get canvas IHTMLElement and IHTMLElement2 interfaces
    CComPtr<IHTMLElement> canvas;
    CComPtr<IHTMLElement2> canvas2;
    if (!GetCanvas(document2, document3, &canvas, &canvas2))
      return E_FAIL;

    // Get the top window dimensions (outerWidth/outerHeight)
    int windowWidth(0), windowHeight(0);
    if (!GetWindowSize(ie_window_handle, &windowWidth, &windowHeight))
      return E_FAIL;
    LOG(DEBUG) << "Initial window size (w, h): " << windowWidth << ", " << windowHeight;

    // Get the view dimensions (innerWidth/innerHeight)
    int viewWidth(0), viewHeight(0);
    if (!GetWindowSize(content_window_handle, &viewWidth, &viewHeight))
      return E_FAIL;
    LOG(DEBUG) << "Initial view size (w, h): " << viewWidth << ", " << viewHeight;

    // The resize message is being ignored if the window appears to be
    // maximized.  There's likely a way to bypass that. The kludgy way
    // is to unmaximize the window, then move on with setting the window
    // to the dimensions we really want.  This is okay because we revert
    // back to the original dimensions afterward.
    BOOL is_maximized = ::IsZoomed(ie_window_handle);

    // GDI+ limit after which it may report Generic error for some image types
    int SIZE_LIMIT = 65534;

    HRESULT hr;
    bool is_resized_width = false;
    bool is_resized_height = false;
    long targetWindowWidth = windowWidth;
    long targetWindowHeight = windowHeight;

    // Get metrics related to the width (clientWidth, scrollWidth, scrollbarWidth)
    long clientWidth(0);
    canvas2->get_clientWidth(&clientWidth);
    long scrollWidth(0);
    canvas2->get_scrollWidth(&scrollWidth);
    int scrollbarWidth = max(0, viewWidth - clientWidth);
    LOG(DEBUG) << "Initial"
      << " clientWidth=" << clientWidth
      << " scrollWidth=" << scrollWidth
      << " scrollbarWidth=" << scrollbarWidth;

    // Increase the window width if necessary.
    int targetViewWidth = max(viewWidth, scrollWidth + scrollbarWidth);
    if (targetViewWidth > viewWidth) {
      if (targetViewWidth  > SIZE_LIMIT) {
        LOG(WARN) << "Required width is greater than limit. Truncating screenshot width.";
        targetViewWidth = SIZE_LIMIT;
      }

      if (is_maximized) {
        LOG(DEBUG) << "Window is maximized currently. Demaximizing.";
        ::ShowWindow(ie_window_handle, SW_SHOWNOACTIVATE);
      }

      targetWindowWidth += (targetViewWidth - viewWidth);
      LOG(DEBUG) << "Increasing window width by " << targetWindowWidth << "px";
      SetWindowSize(ie_window_handle, targetWindowWidth, targetWindowHeight);

      is_resized_width = true;
    }

    // Get metrics related to the height (clientHeight, scrollHeight, scrollbarHeight)
    long clientHeight(0);
    canvas2->get_clientHeight(&clientHeight);
    long scrollHeight(0);
    canvas2->get_scrollHeight(&scrollHeight);
    int scrollbarHeight = max(0, viewHeight - clientHeight);
    LOG(DEBUG) << "Initial"
      << " clientHeight=" << clientHeight
      << " scrollHeight=" << scrollHeight
      << " scrollbarHeight=" << scrollbarHeight;

    // Increase the window height if necessary.
    int targetViewHeight = max(viewHeight, scrollHeight + scrollbarHeight);
    if (targetViewHeight > viewHeight) {
      if (targetViewHeight > SIZE_LIMIT) {
        LOG(WARN) << "Required height is greater than limit. Truncating screenshot height.";
        targetViewHeight = SIZE_LIMIT;
      }

      if (is_maximized && !is_resized_width) {
        LOG(DEBUG) << "Window is maximized currently. Demaximizing.";
        ::ShowWindow(ie_window_handle, SW_SHOWNOACTIVATE);
      }

      if (scrollbarWidth > 0) {
        // Force the vertical scrollbar by removing 2 pixels so it
        // doesn't disappear once resized.
        targetViewHeight -= 2;
        LOG(DEBUG) << "Removed 2px to the targeted height to force the vertical scrollbar.";
      }

      targetWindowHeight += (targetViewHeight - viewHeight);
      LOG(DEBUG) << "Increasing window height by " << targetWindowHeight << "px";
      SetWindowSize(ie_window_handle, targetWindowWidth, targetWindowHeight);

      is_resized_height = true;
    }

    // Get the final client size.
    long targetClientWidth = clientWidth;
    long targetClientHeight = clientHeight;
    if (is_resized_width || is_resized_height) {
      // In some rare cases, the client size is not yet updated.
      // If it's the case, we force the reclac and retry.
      for (int i = 0; i < 2; i++) {
        canvas2->get_clientWidth(&targetClientWidth);
        canvas2->get_clientHeight(&targetClientHeight);

        // Check that the target client width/height has been updated.
        bool calcWidthOK = !is_resized_width || targetClientWidth != clientWidth;
        bool calcHeightOK = !is_resized_height || targetClientHeight != clientHeight;
        if (calcWidthOK && calcHeightOK)
          break;

        LOG(DEBUG) << "Failed to update the client size at try " << i;

        // recalc document
        bool fForce = i > 0;
        document3->recalc(fForce);
      }
    }

    // Ensure that the client area has at least 1 pixel.
    // If it's not the case, we take the view size as target instead.
    if (targetClientWidth < 1 || targetClientHeight < 1) {
      LOG(WARN) << "Target client size is null. Take the view size instead.";
      targetClientWidth = targetViewWidth;
      targetClientHeight = targetViewHeight;
    }

    LOG(DEBUG) << "Final client size: " << targetClientWidth << " x " << targetClientHeight;
    LOG(DEBUG) << "Final view size: " << targetViewWidth << " x " << targetViewHeight;
    LOG(DEBUG) << "Final window size: " << targetWindowWidth << " x " << targetWindowHeight;

    // Capture the view
    this->image_ = CaptureView(content_window_handle,
                               targetClientWidth,
                               targetClientHeight,
                               clientWidth - 17,
                               clientHeight - 17);

    if (is_resized_width || is_resized_height) {
      // Restore the browser to the original dimensions.
      if (is_maximized) {
        ::ShowWindow(ie_window_handle, SW_MAXIMIZE);
      } else {
        SetWindowSize(ie_window_handle, windowWidth, windowHeight);
      }
    }

    return this->image_ == NULL ? E_FAIL : S_OK;
  }


  /// <summary>
  /// Capture the view window (Internet Explorer_Server) using the PrintWindow API.
  /// Returns a CImage pointer if succeed, NULL otherwise.
  /// </summary>
  CImage* CaptureView(const HWND view_handle, int width, int height
    , int check_width, int check_height) {

    // Create the bitmap (32bits per pixel) and get the device context.
    CImage* image = new CImage();
    if (!image->Create(width, height, 32, 0)) {
      LOG(WARN) << "Unable to initialize image object";
      return NULL;
    }
    HDC device_context_handle = image->GetDC();

    // Enter a try loop to capture the view (3 tries). The capture is considered a
    // success if within the check size, at least one pixel is different.
    for (int i = 1; i <= 3; i++) {

      bool is_print_success = ::PrintWindow(view_handle, device_context_handle, 0);
      if (!is_print_success) {
        LOG(WARN) << "PrintWindow API failed at try " << i;
        ::UpdateWindow(view_handle);
        continue;
      }

      bool is_same_color = IsImageSameColour(image, check_width, check_height);
      if (is_same_color) {
        LOG(DEBUG) << "Failed to capture non single colour browser image at try " << i;
        ::UpdateWindow(view_handle);
        continue;
      }

      break;
    }

    // Release the device context and return the image.
    image->ReleaseDC();
    return image;
  }


  /// <summary>
  /// Get the document IHTMLDocument2 and IHTMLDocument3 interfaces from the browser.
  /// Return true if succeed, false otherwise.
  /// </summary>
  static bool GetDocument(BrowserHandle browser, IHTMLDocument2** document2
    , IHTMLDocument3** document3) {

    // Get document IHTMLDocument2 interface from browser
    browser->GetDocument(true, document2);
    if (!document2) {
      LOG(WARN) << "Unable to get document from browser. Are you viewing a non-HTML document?";
      return false;
    }

    // Get document IHTMLDocument3 interface
    HRESULT hr = (*document2)->QueryInterface<IHTMLDocument3>(document3);
    if (FAILED(hr)) {
      LOGHR(WARN, hr) << "Unable to get IHTMLDocument3 interface from document.";
      return false;
    }

    return true;
  }


  /// <summary>
  /// Get the canvas which is the body if the document is in compatible mode or
  /// the documentElement otherwise.
  /// Return true if succeed, false otherwise.
  /// </summary>
  static bool GetCanvas(IHTMLDocument2* document2, IHTMLDocument3* document3
    , IHTMLElement** canvas, IHTMLElement2** canvas2) {

    HRESULT hr;

    bool isStandardMode = DocumentHost::IsStandardsMode(document2);
    if (isStandardMode) {
      // Canvas is documentElement
      hr = document3->get_documentElement(canvas);
      if (FAILED(hr)) {
        LOGHR(WARN, hr) << "Unable to get documentElement from document.";
        return false;
      }
    } else {
      // Canvas is body
      hr = document2->get_body(canvas);
      if (FAILED(hr)) {
        LOGHR(WARN, hr) << "Unable to get body from document.";
        return false;
      }
    }

    // Get canvas IHTMLElement2 interface
    hr = (*canvas)->QueryInterface<IHTMLElement2>(canvas2);
    if (FAILED(hr)) {
      LOGHR(WARN, hr) << "Unable to get IHTMLElement2 interface from canvas.";
      return false;
    }

    return true;
  }

};

} // namespace webdriver


#endif // WEBDRIVER_IE_SCREENSHOTCOMMANDHANDLER_H_
