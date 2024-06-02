/*
 * Copyright (C) 2024 Piotr Brzeziński <piotr@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without esven the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

import Collections
import Foundation
import ScreenCaptureKit

let DEFAULT_FPS: Int32 = 30

@available(macOS 12.3, *)
@objc public class SCKitVideoSrc: NSObject, SCKitSrc {
  private let videoSampleBufferQueue = DispatchQueue(
    label: "gst-sckit-videoQueue", qos: .userInteractive)

  private var CAT: UnsafeMutablePointer<GstDebugCategory>
  private var baseSrc: UnsafeMutablePointer<GstBaseSrc>

  // Using GMutex/GCond because Swift doesn't have a native condvar
  private var lastSampleLock: GMutex = GMutex()
  private var lastSampleCond: GCond = GCond()
  // If we get a new idle frame, we duplicate the last successful one but with the idle's PTS
  private var lastSample: CMSampleBuffer?
  private var lastFrameTimestamp: CMTime?
  private var unlockRequested: Bool = false

  /// Protects scStream/scConfig (can be changed from create()/setCaps() or reconfigure signal)
  private var configLock: GMutex = GMutex()
  private var scConfig: SCStreamConfiguration?
  private var scStream: SCStream?
  private var scClock: CMClock?
  private var scOutputHandler: SCKitSrcOutputHandler!
  private var videoInfo: GstVideoInfo?

  private var currentOutputRes: (Int, Int)?
  private var firstBufferPts = CMTime.zero
  private var isStreamConfigured: Bool {
    return self.scConfig != nil
  }
  private var isWindowCaptureMode: Bool {
    return self.captureMode == GST_SCKIT_VIDEO_SRC_MODE_WINDOW
  }

  @objc public var captureMode: GstSCKitVideoSrcMode = GstSCKitVideoSrcMode(
    UInt32(DEFAULT_CAPTURE_MODE))
  @objc public var displayID: Int32 = DEFAULT_DISPLAY_ID
  @objc public var windowIDs: [CGWindowID] = []
  @objc public var applicationIDs: [pid_t] = []
  @objc public var showCursor: Bool = (DEFAULT_SHOW_CURSOR == 1)
  @objc public var allowTransparency: Bool = (DEFAULT_ALLOW_TRANSPARENCY == 1)
  @objc public var cropRect: CGRect = CGRect.zero

  @objc public init(
    src: UnsafeMutablePointer<GstBaseSrc>, debugCat: UnsafeMutablePointer<GstDebugCategory>
  ) {
    CAT = debugCat
    self.baseSrc = src
    super.init()

    g_mutex_init(&configLock)
    g_mutex_init(&lastSampleLock)
    g_cond_init(&lastSampleCond)

    self.scOutputHandler = SCKitSrcOutputHandler(src: self, streamType: .screen)

    gst_base_src_set_live(src, Int32(true))
    gst_base_src_set_format(src, GST_FORMAT_TIME)
  }

  deinit {
    g_mutex_clear(&configLock)
    g_mutex_clear(&lastSampleLock)
    g_cond_clear(&lastSampleCond)
  }

  private func start() -> Bool {
    #gstLog(CAT, "start called")

    g_mutex_lock(&configLock)
    defer { g_mutex_unlock(&configLock) }

    if !self.configureCapture() {
      #gstElementError(
        CAT, baseSrc, GstResourceError.failed, "Failed to configure capture",
        "Check if you have screen capture permissions")
      return false
    }

    do {
      try waitFor { self.scStream!.startCapture(completionHandler: $0) }
    } catch {
      #gstElementError(
        CAT, baseSrc, GstResourceError.failed, "Failed to start capture",
        "SCKit error: \(error)")
      return false
    }

    return true
  }

  private func stop() -> Bool {
    #gstLog(CAT, "stop called")
    g_mutex_lock(&configLock)
    if let scStream = self.scStream {
      try? waitFor { scStream.stopCapture(completionHandler: $0) }
      self.scStream = nil
      self.scConfig = nil
      self.scClock = nil
    }
    g_mutex_unlock(&configLock)

    g_mutex_lock(&lastSampleLock)
    self.lastSample = nil
    self.lastFrameTimestamp = nil
    g_mutex_unlock(&lastSampleLock)

    return true
  }

  private func setCaps(caps: UnsafeMutablePointer<GstCaps>) -> Bool {
    var videoInfo = GstVideoInfo()
    guard gst_video_info_from_caps(&videoInfo, caps) != 0 else {
      #gstError(CAT, "Failed to create video info from caps")
      return false
    }

    #gstTrace(CAT, "Received new video info: \(videoInfo)")
    self.videoInfo = videoInfo
    gst_base_src_set_blocksize(baseSrc, guint(videoInfo.size))

    g_mutex_lock(&configLock)
    defer { g_mutex_unlock(&configLock) }
    if let scConfig = self.scConfig {
      setupConfig(scConfig, basedOn: videoInfo)
      do {
        try waitFor { self.scStream!.updateConfiguration(scConfig, completionHandler: $0) }
      } catch {
        #gstError(CAT, "Failed to update stream config for new output caps: \(error)")
        return false
      }
    }

    return true
  }

  private func getCaps(filter: UnsafeMutablePointer<GstCaps>?) -> UnsafeMutablePointer<
    GstCaps
  > {
    #gstLog(CAT, "getCaps called")
    let srcpad = self.baseSrc.pointee.srcpad
    let templateCaps = gst_pad_get_pad_template_caps(srcpad)!

    guard let scConfig = scConfig else {
      #gstLog(CAT, "Stream not configured yet, returning template caps")
      return templateCaps
    }

    // At the very beginning of the capture, let's report what SCKit is configured to capture.
    // As soon as we get a buffer, switch to reporting the actual resolution,
    // because scConfig values could be modified to match content size in the meantime
    // (but not yet take effect output-wise!)
    let (width, height) = self.currentOutputRes ?? (scConfig.width, scConfig.height)

    // TODO: Issue with Swift<->C interop: have to set those fields separately.
    // Swift seems to have lifetime issues with strings in va_list.
    // At some point the pointer ends up being NULL, despite code checking for it...
    let caps = gstCapsMakeWritable(templateCaps)
    gstCapsSetSimple(
      caps: caps, field: "width", G_TYPE_INT, width)
    gstCapsSetSimple(
      caps: caps, field: "height", G_TYPE_INT, height)

    if let filterCaps = filter {
      let intersection = gst_caps_intersect_full(filterCaps, caps, GST_CAPS_INTERSECT_FIRST)!
      gst_caps_unref(caps)
      return intersection
    }

    return caps
  }

  private func fixateCaps(caps: UnsafeMutablePointer<GstCaps>) -> UnsafeMutablePointer<GstCaps> {
    #gstLog(CAT, "fixateCaps called")
    let caps = gstCapsMakeWritable(caps)

    for i in 0..<gst_caps_get_size(caps) {
      let s = gst_caps_get_structure(caps, i)!
      gst_structure_fixate_field_nearest_fraction(s, "framerate", DEFAULT_FPS, 1)
    }

    return gst_caps_fixate(caps)
  }

  private func create(
    offset: guint64, size: guint, bufPtr: UnsafeMutablePointer<UnsafeMutablePointer<GstBuffer>>
  ) -> GstFlowReturn {
    #gstLog(CAT, "create called")

    guard let videoInfo = self.videoInfo,
      videoInfo.fps_n > 0, videoInfo.fps_d > 0
    else {
      return GST_FLOW_NOT_NEGOTIATED
    }

    g_mutex_lock(&lastSampleLock)
    while !self.unlockRequested && self.lastFrameTimestamp == nil {
      g_cond_wait(&lastSampleCond, &lastSampleLock)
    }

    if self.unlockRequested {
      g_mutex_unlock(&lastSampleLock)
      return GST_FLOW_FLUSHING
    }

    // SCKit generates either frames with an image buffer, or idle frames with just timing info
    // if nothing changed on screen. If we get an idle frame, we send the last 'full' one again,
    // but with the idle frame's PTS. lastFrameTimestamp is used to indicate that we got a frame
    // since the last create() call.
    let sampleBuffer = self.lastSample
    let samplePts = self.lastFrameTimestamp
    self.lastFrameTimestamp = nil
    g_mutex_unlock(&lastSampleLock)

    guard let sampleBuffer = sampleBuffer,
      let samplePts = samplePts, sampleBuffer.isValid
    else {
      #gstError(CAT, "Failed to get a sample buffer!")
      return GST_FLOW_ERROR
    }

    guard
      let attachmentsArray = CMSampleBufferGetSampleAttachmentsArray(
        sampleBuffer,
        createIfNecessary: false) as? [[SCStreamFrameInfo: Any]],
      let attachments = attachmentsArray.first
    else {
      #gstError(CAT, "Failed to get buffer attachments to determine capture size and scale!")
      return GST_FLOW_ERROR
    }

    guard let contentRectDict = attachments[.contentRect],
      let contentRect = CGRect(dictionaryRepresentation: contentRectDict as! CFDictionary),
      let scaleFactor = attachments[.scaleFactor] as? CGFloat,
      let pointsToPixels = attachments[.contentScale] as? CGFloat
    else {
      #gstError(CAT, "Failed to get captured content size and/or scale!")
      return GST_FLOW_ERROR
    }

    #gstTrace(
      CAT,
      "contentRect: \(contentRect), scale: \(scaleFactor), pointsToPixels: \(pointsToPixels)"
    )

    // Now, let's check the content size itself.
    // It's separate from the output resolution, e.g. not all of the frame has to be
    // filled with content. We change the desired output size to match content size,
    // so the output nicely matches a window being resized, for example.
    let (contentWidth, contentHeight): (Int, Int) = (
      Int(contentRect.size.width / pointsToPixels * scaleFactor),
      Int(contentRect.size.height / pointsToPixels * scaleFactor)
    )

    g_mutex_lock(&configLock)
    if !self.isStreamConfigured {
      #gstElementError(
        CAT, baseSrc, GstResourceError.failed, "Stream not configured",
        "Stream probably stopped because of an external error")
      g_mutex_unlock(&configLock)
      return GST_FLOW_ERROR
    }

    // For NV12, we need to ensure that output width and height are even, otherwise
    // the CVPixelBuffers we get can have missing padding despite reporting it's there.
    // Using destinationRect ensures that content is not stretched to fit the padded frame.
    let shouldUpdate =
      if self.scConfig!.destinationRect != CGRect.zero {
        self.scConfig!.destinationRect.size.width != CGFloat(contentWidth)
          || self.scConfig!.destinationRect.size.height != CGFloat(contentHeight)
      } else {
        self.scConfig!.width != contentWidth || self.scConfig!.height != contentHeight
      }

    if shouldUpdate {
      #gstLog(CAT, "Content size changed to \(contentWidth)x\(contentHeight), updating config")

      if self.isNV12(self.scConfig!) && (contentWidth % 2 != 0 || contentHeight % 2 != 0) {
        self.scConfig!.destinationRect = CGRect(
          x: 0, y: 0, width: CGFloat(contentWidth), height: CGFloat(contentHeight))
        self.scConfig!.width = contentWidth % 2 == 0 ? contentWidth : contentWidth + 1
        self.scConfig!.height = contentHeight % 2 == 0 ? contentHeight : contentHeight + 1
      } else {
        // .zero here means to use the whole output frame
        self.scConfig!.destinationRect = CGRect.zero
        self.scConfig!.width = contentWidth
        self.scConfig!.height = contentHeight
      }

      do {
        // Might take a few frames for SCKit to actually output with updated size, that's checked for below
        try waitFor { self.scStream!.updateConfiguration(scConfig!, completionHandler: $0) }
      } catch {
        #gstError(CAT, "Failed to update stream config for new content size: \(error)")
        g_mutex_unlock(&configLock)
        return GST_FLOW_ERROR
      }

      #gstDebug(
        CAT,
        "Output config changed to \(self.scConfig!.width)x\(self.scConfig!.height), destinationRect: \(self.scConfig!.destinationRect)"
      )
    }

    // This uses scStream.synchronizationClock (if available), so let's also call it while the lock is held
    let timestamp = calculateGstPts(from: samplePts)
    g_mutex_unlock(&configLock)

    guard let buffer = gst_core_media_buffer_new(sampleBuffer, 1, nil) else {
      #gstError(CAT, "Failed to create a CoreMedia buffer!")
      return GST_FLOW_ERROR
    }

    // Track the actual output resolution, in case the desired output size was previously changed
    // due to a window resizing, display resolution change, or even whole capture mode change
    let imageBuffer = CMSampleBufferGetImageBuffer(sampleBuffer)!
    let width = CVPixelBufferGetWidth(imageBuffer)
    let height = CVPixelBufferGetHeight(imageBuffer)
    self.currentOutputRes = (width, height)

    if self.currentOutputRes! != (Int(videoInfo.width), Int(videoInfo.height)) {
      #gstDebug(
        CAT, "Output res differs from configured res, negotiating with \(self.currentOutputRes!)")

      if gst_base_src_negotiate(baseSrc) == 0 {
        #gstError(CAT, "Failed to negotiate with new output res: \(self.currentOutputRes!)")
        return GST_FLOW_ERROR
      }
    }

    buffer.pointee.duration = 0
    buffer.pointee.pts = timestamp
    bufPtr.pointee = buffer
    return GST_FLOW_OK
  }

  /// Used for the initial capture configuration, as well as reconfiguring the session
  /// on any property change. Ony bypassed in setCaps() for framerate and in create()
  /// for changing output dimensions on the fly.
  ///
  /// Should be called with configLock held.
  private func configureCapture() -> Bool {
    #gstLog(CAT, "configureCapture called")
    guard
      let scContent = try? waitFor(function: {
        SCShareableContent.getWithCompletionHandler($0)
      })
    else {
      #gstError(CAT, "Missing permissions for screen capture!")
      return false
    }

    guard
      let scDisplay =
        if displayID == DEFAULT_DISPLAY_ID {
          scContent.displays.first
        } else {
          getSCDisplay(from: scContent, by: CGDirectDisplayID(displayID))
        }
    else {
      #gstError(CAT, "Couldn't find display to capture from")
      return false
    }

    let scFilter: SCContentFilter
    switch self.captureMode {
    case GST_SCKIT_VIDEO_SRC_MODE_DISPLAY:
      scFilter = SCContentFilter(display: scDisplay, excludingWindows: [])

    case GST_SCKIT_VIDEO_SRC_MODE_WINDOW:
      guard self.windowIDs.count == 1 else {
        #gstError(CAT, "Window capture mode requires exactly one window ID!")
        return false
      }

      guard let scWindow = getSCWindow(from: scContent, by: self.windowIDs[0]) else {
        #gstError(CAT, "Couldn't find window to capture from")
        return false
      }

      scFilter = SCContentFilter(desktopIndependentWindow: scWindow)

    case GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_EXCLUDING_WINDOWS:
      let scExcludedWindows = self.windowIDs.compactMap { windowID -> SCWindow? in
        guard let window = getSCWindow(from: scContent, by: windowID) else {
          #gstError(CAT, "Couldn't find window to exclude: ID \(windowID)")
          return nil
        }
        return window
      }

      scFilter = SCContentFilter(display: scDisplay, excludingWindows: scExcludedWindows)

    case GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_INCLUDING_WINDOWS:
      let scIncludedWindows = self.windowIDs.compactMap { windowID -> SCWindow? in
        guard let window = getSCWindow(from: scContent, by: windowID) else {
          #gstError(CAT, "Couldn't find window to include: ID \(windowID)")
          return nil
        }
        return window
      }

      scFilter = SCContentFilter(display: scDisplay, including: scIncludedWindows)

    case GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_INCLUDING_APPLICATIONS_EXCEPT_WINDOWS:
      let scIncludedApplications = self.applicationIDs.compactMap {
        processID -> SCRunningApplication? in
        guard let application = getSCApplication(from: scContent, by: processID) else {
          #gstError(CAT, "Couldn't find application to include: PID \(processID)")
          return nil
        }
        return application
      }

      let scExceptedWindows = self.windowIDs.compactMap { windowID -> SCWindow? in
        guard let window = getSCWindow(from: scContent, by: windowID) else {
          #gstError(CAT, "Couldn't find window to except from filter: ID \(windowID)")
          return nil
        }
        return window
      }

      scFilter = SCContentFilter(
        display: scDisplay, including: scIncludedApplications,
        exceptingWindows: scExceptedWindows)

    case GST_SCKIT_VIDEO_SRC_MODE_DISPLAY_EXCLUDING_APPLICATIONS_EXCEPT_WINDOWS:
      let scExcludedApplications = self.applicationIDs.compactMap {
        processID -> SCRunningApplication? in
        guard let application = getSCApplication(from: scContent, by: processID) else {
          #gstError(CAT, "Couldn't find application to exclude: PID \(processID)")
          return nil
        }
        return application
      }

      let scExceptedWindows = self.windowIDs.compactMap { windowID -> SCWindow? in
        guard let window = getSCWindow(from: scContent, by: windowID) else {
          #gstError(CAT, "Couldn't find window to except from filter: ID \(windowID)")
          return nil
        }
        return window
      }

      scFilter = SCContentFilter(
        display: scDisplay, excludingApplications: scExcludedApplications,
        exceptingWindows: scExceptedWindows)

    default:
      #gstError(CAT, "Unknown capture mode: \(self.captureMode)")
      return false
    }

    let scConfig = SCStreamConfiguration()
    scConfig.scalesToFit = true
    scConfig.showsCursor = self.showCursor
    scConfig.queueDepth = 5
    if #available(macOS 14.0, *) {
      scConfig.shouldBeOpaque = !self.allowTransparency
    }

    // Will only set framerate for now. Might add more (e.g. color space) later.
    // Res is directly reported in getCaps and non-negotiable for now.
    // Internal scaling (activated by downstream caps negotiation) can be supported later if needed.
    if let videoInfo = videoInfo {
      setupConfig(scConfig, basedOn: videoInfo)
    } else {
      scConfig.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
      scConfig.minimumFrameInterval = CMTime(value: 1, timescale: DEFAULT_FPS)
    }

    // Before macOS 14.0, we can't get the scale factor directly from the filter.
    // For displays, we can just retrieve it via the screen ID.
    // For windows there is no way to access a NSWindow from another app,
    // which would be necessary to know its actual backingScaleFactor.
    // Other ways, such as looking at the window coordinates, are also unreliable.
    // Let's just assume 1x scale and resize if needed once we get the first frame.
    let (contentRect, contentScale): (CGRect, CGFloat) =
      if #available(macOS 14.0, *) {
        (scFilter.contentRect, CGFloat(scFilter.pointPixelScale))
      } else if self.isWindowCaptureMode {
        (getSCWindow(from: scContent, by: self.windowIDs[0])!.frame, 1.0)
      } else {
        (
          scDisplay.frame,
          self.nsScreenFrom(displayID: scDisplay.displayID)?.backingScaleFactor ?? 1.0
        )
      }

    var outputWidth = Int(contentRect.size.width * contentScale)
    var outputHeight = Int(contentRect.size.height * contentScale)

    // For documentation purposes:
    // https://developer.apple.com/documentation/screencapturekit/scstreamconfiguration/3919829-sourcerect
    // sourceRect is the region of the content to capture, seems to be in points (not pixels, so not 2x on retina)
    // How it works varies by capture mode:
    // - in window capture mode, it's supposed to be completely ignored according to docs.
    //   However, it seems to work (at least on macOS 14.5), and it's always respected even if out-of bounds.
    //   This means that if the crop region is outside of the window, we will only get 'blank' frames (no image buffer)
    //   and only parts of the window that reach the crop region (e.g. after resizing) will be captured.
    //   The crop region is never moved (relative to the window) or reset, no matter how the window is resized.
    // - in display capture mode, it's the region of the display to capture.
    //   If the crop region partially goes beyond the display bounds, the output will either be cropped correctly
    //   and report correct contentRect, OR it will end up with the area beyond the display padded with black, AND
    //   the output frame will have the resolution of the display, while the actual display area will be scaled down
    //   so that both it and the padded area fit into the output frame. The second scenario usually happens if one
    //   of the dimensions (w/h) of the crop region is larger than the display's. Sometimes I even got it to segfault there.
    //   Normal in-bounds cropping works as expected.
    //   If x/y coordinates are outside of the display, there will be no output ('blank' frames).
    if self.cropRect != CGRect.zero {
      let screenWidth = contentRect.size.width
      let screenHeight = contentRect.size.height

      // According to docs sourceRect should be ignored in window capture mode.
      // That's not the case for me on macOS 14.5, but let's disable cropping in that mode
      // just in case it starts acting as documented in the future.
      if self.isWindowCaptureMode {
        #gstWarning(CAT, "Cropping is not supported in window capture mode, ignoring")
      } else if (self.cropRect.size.width == 0) || (self.cropRect.size.height == 0) {
        #gstWarning(CAT, "Crop width or height is 0, ignoring")
        // All the possible scenarios are separate here because combining them with ||
        // made the compiler (Swift 5.10) crash for me. Might be fixed in 6.0, didn't check yet.
      } else if self.cropRect.origin.x >= screenWidth || self.cropRect.origin.y >= screenHeight {
        #gstWarning(CAT, "Crop origin is outside of screen, ignoring")
      } else if (self.cropRect.origin.x + self.cropRect.size.width) > screenWidth {
        #gstWarning(CAT, "Crop area goes beyond screen width, ignoring")
      } else if (self.cropRect.origin.y + self.cropRect.size.height) > screenHeight {
        #gstWarning(CAT, "Crop area goes beyond screen height, ignoring")
      } else {
        // Weirdly enough, CGRect usually has the origin at the bottom left, but here it's treated as the usual top left :)
        scConfig.sourceRect = self.cropRect
        outputWidth = Int(self.cropRect.size.width * contentScale)
        outputHeight = Int(self.cropRect.size.height * contentScale)
      }
    }

    // NV12 output is sometimes incorrect with odd resolutions, the buffers we get from SCKit
    // don't have padding even though they seem to report otherwise. Due to that, let's pad
    // output res to have even values, and set destinationRect to the original res to avoid stretching.
    // scaleToFit could take care of this, but according to docs it works only for window capture.
    // Let's use destinationRect instead to be sure.
    if self.isNV12(scConfig) && (outputWidth % 2 != 0 || outputHeight % 2 != 0) {
      #gstInfo(CAT, "Output format is NV12 with odd resolution, adding padding")
      scConfig.destinationRect = CGRect(
        x: 0, y: 0, width: outputWidth, height: outputHeight)

      if outputWidth % 2 != 0 { outputWidth += 1 }
      if outputHeight % 2 != 0 { outputHeight += 1 }
    }

    scConfig.width = outputWidth
    scConfig.height = outputHeight
    #gstLog(
      CAT,
      "Output size: \(outputWidth)x\(outputHeight), sourceRect: \(scConfig.sourceRect), destinationRect: \(scConfig.destinationRect)"
    )

    if self.isStreamConfigured {
      // According to https://developer.apple.com/documentation/screencapturekit/capturing_screen_content_in_macos#4315331,
      // you can change the configuration of an ongoing capture without stopping it.
      do {
        try waitFor { self.scStream!.updateConfiguration(scConfig, completionHandler: $0) }
        try waitFor { self.scStream!.updateContentFilter(scFilter, completionHandler: $0) }
      } catch {
        #gstError(CAT, "Failed to update stream config/filter: \(error)")
        return false
      }
    } else {
      do {
        let scStream = SCStream(
          filter: scFilter, configuration: scConfig, delegate: self.scOutputHandler)
        try scStream.addStreamOutput(
          scOutputHandler, type: .screen, sampleHandlerQueue: videoSampleBufferQueue)
        self.scStream = scStream
      } catch {
        #gstError(CAT, "Failed to add stream output: \(error)")
        return false
      }
    }

    if #available(macOS 13.0, *) {
      self.scClock = self.scStream?.synchronizationClock
    } else {
      #gstWarning(
        CAT, "macOS version <13.0, buffers will not be timestamped correctly!")
    }

    self.scConfig = scConfig
    return true
  }

  /// Should be called with configLock held.
  private func setupConfig(_ config: SCStreamConfiguration, basedOn videoInfo: GstVideoInfo) {
    config.minimumFrameInterval = CMTime(
      value: Int64(videoInfo.fps_d), timescale: Int32(videoInfo.fps_n))

    switch videoInfo.finfo.pointee.format {
    case GST_VIDEO_FORMAT_NV12 where videoInfo.colorimetry.range == GST_VIDEO_COLOR_RANGE_0_255:
      config.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
    case GST_VIDEO_FORMAT_NV12:
      config.pixelFormat = kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
    case GST_VIDEO_FORMAT_BGRA:
      config.pixelFormat = kCVPixelFormatType_32BGRA
    default:
      #gstError(CAT, "Unsupported video format: \(videoInfo.finfo.pointee.format)")
    }

    // TODO: possible to add colorMatrix, colorSpaceName
  }

  /// Should be called with configLock held.
  func calculateGstPts(
    from samplePts: CMTime
  ) -> GstClockTime {
    guard let scClock = self.scClock else {
      return GST_CLOCK_TIME_NONE
    }

    let (ourClock, baseTime) = self.baseSrc.withMemoryRebound(to: GstElement.self, capacity: 1) {
      (gst_element_get_clock($0), gst_element_get_base_time($0))
    }

    guard let ourClock = ourClock else {
      #gstError(CAT, "Couldn't get pipeline clock!")
      return GST_CLOCK_TIME_NONE
    }

    if firstBufferPts.value == 0 {
      firstBufferPts = samplePts
    }

    let actualPts = CMTimeSubtract(samplePts, firstBufferPts)
    let sckitPts = gst_util_uint64_scale(
      GST_SECOND, UInt64(actualPts.value),
      UInt64(actualPts.timescale))

    let now = scClock.time
    let gstNow = gst_util_uint64_scale(
      GST_SECOND, UInt64(now.value), UInt64(now.timescale))
    let gstNowDiff = gstNow - sckitPts

    let runningTime = gst_clock_get_time(ourClock) - baseTime
    let timestamp = runningTime >= gstNowDiff ? runningTime - gstNowDiff : runningTime

    return timestamp
  }

  private func unlock() -> Bool {
    g_mutex_lock(&lastSampleLock)
    self.unlockRequested = true
    g_cond_signal(&lastSampleCond)
    g_mutex_unlock(&lastSampleLock)

    return true
  }

  private func unlockStop() -> Bool {
    g_mutex_lock(&lastSampleLock)
    self.unlockRequested = false
    g_mutex_unlock(&lastSampleLock)

    return true
  }

  private func getSCDisplay(from content: SCShareableContent, by displayID: CGDirectDisplayID)
    -> SCDisplay?
  {
    return content.displays.first { $0.displayID == displayID }
  }

  private func getSCWindow(from content: SCShareableContent, by windowID: CGWindowID) -> SCWindow? {
    return content.windows.first { $0.windowID == windowID }
  }

  private func getSCApplication(from content: SCShareableContent, by processID: pid_t)
    -> SCRunningApplication?
  {
    return content.applications.first { $0.processID == processID }
  }

  public func handleBuffer(sampleBuffer: CMSampleBuffer) {
    g_mutex_lock(&lastSampleLock)
    self.lastSample = sampleBuffer
    self.lastFrameTimestamp = sampleBuffer.outputPresentationTimeStamp
    g_cond_signal(&lastSampleCond)
    g_mutex_unlock(&lastSampleLock)
  }

  public func handleIdle(pts: CMTime) {
    g_mutex_lock(&lastSampleLock)
    self.lastFrameTimestamp = pts
    g_cond_signal(&lastSampleCond)
    g_mutex_unlock(&lastSampleLock)
  }

  public func handleStop(with error: Error) {
    print("Stream stopped because of an external error: \(error)")
    #gstElementError(
      CAT, baseSrc, GstResourceError.failed, "Stream stopped because of an external error",
      "SCKit error: \(error)")

    g_mutex_lock(&configLock)
    self.scStream = nil
    self.scConfig = nil
    self.scClock = nil
    g_mutex_unlock(&configLock)
  }

  private func handleReconfigureSignal() {
    g_mutex_lock(&configLock)
    if self.isStreamConfigured {
      #gstDebug(CAT, "Reconfigure signal received, reconfiguring capture")
      if !self.configureCapture() {
        #gstElementError(
          CAT, baseSrc, GstResourceError.failed, "Failed to reconfigure capture",
          "New property value caused an error")
      }
    } else {
      #gstDebug(CAT, "Stream not configured yet, ignoring reconfigure signal")
    }
    g_mutex_unlock(&configLock)
  }

  private func isNV12(_ scConfig: SCStreamConfiguration) -> Bool {
    return scConfig.pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarFullRange
      || scConfig.pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange
  }

  func nsScreenFrom(displayID: CGDirectDisplayID) -> NSScreen? {
    for screen in NSScreen.screens {
      if let screenID = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")]
        as? CGDirectDisplayID
      {
        if screenID == displayID {
          return screen
        }
      }
    }
    return nil
  }

  /**************************** Objective-C API ************************************************/
  @objc public func gstStart() -> Bool {
    return self.start()
  }

  @objc public func gstStop() -> Bool {
    return self.stop()
  }

  @objc public func gstCreate(
    _ offset: guint64, size: guint, bufPtr: UnsafeMutablePointer<UnsafeMutablePointer<GstBuffer>>
  ) -> GstFlowReturn {
    return self.create(offset: offset, size: size, bufPtr: bufPtr)
  }

  @objc public func gstSetCaps(_ caps: UnsafeMutablePointer<GstCaps>) -> Bool {
    return self.setCaps(caps: caps)
  }

  @objc public func gstGetCaps(_ filter: UnsafeMutablePointer<GstCaps>?) -> UnsafeMutablePointer<
    GstCaps
  > {
    return self.getCaps(filter: filter)
  }

  @objc public func gstFixateCaps(_ caps: UnsafeMutablePointer<GstCaps>) -> UnsafeMutablePointer<
    GstCaps
  > {
    return self.fixateCaps(caps: caps)
  }

  @objc public func gstUnlock() -> Bool {
    return self.unlock()
  }

  @objc public func gstUnlockStop() -> Bool {
    return self.unlockStop()
  }

  @objc public func gstHandleReconfigureSignal() {
    self.handleReconfigureSignal()
  }

  @objc public func setCropX(_ x: UInt32) {
    self.cropRect.origin.x = CGFloat(x)
  }

  @objc public func setCropY(_ y: UInt32) {
    self.cropRect.origin.y = CGFloat(y)
  }

  @objc public func setCropWidth(_ width: UInt32) {
    self.cropRect.size.width = CGFloat(width)
  }

  @objc public func setCropHeight(_ height: UInt32) {
    self.cropRect.size.height = CGFloat(height)
  }
}
