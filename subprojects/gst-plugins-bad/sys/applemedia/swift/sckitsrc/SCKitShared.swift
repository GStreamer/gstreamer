import Foundation
import ScreenCaptureKit

@available(macOS 12.3, *)
class SCKitSrcOutputHandler: NSObject, SCStreamOutput, SCStreamDelegate {
  private weak var src: SCKitSrc?
  private var firstBufferPts = CMTime.zero
  private var streamType: SCStreamOutputType

  init(src: SCKitSrc, streamType: SCStreamOutputType) {
    self.src = src
    self.streamType = streamType
    super.init()
  }

  public func stream(
    _ stream: SCStream, didOutputSampleBuffer sampleBuffer: CMSampleBuffer,
    of type: SCStreamOutputType
  ) {
    guard sampleBuffer.isValid else { return }

    // TODO: possibly support two types here (for audio, microphone is a separate type)
    if type != streamType || src == nil {
      return
    }

    // Audio doesn't provide any status etc. info
    // TODO: 15.0+ also has .microphone!
    if #available(macOS 13.0, *) {
      if streamType == .audio {
        src!.handleBuffer(sampleBuffer: sampleBuffer)
      }
    }

    guard
      let attachmentsArray = CMSampleBufferGetSampleAttachmentsArray(
        sampleBuffer,
        createIfNecessary: false) as? [[SCStreamFrameInfo: Any]],
      let attachments = attachmentsArray.first
    else { return }

    guard let statusRawValue = attachments[SCStreamFrameInfo.status] as? Int,
      let status = SCFrameStatus(rawValue: statusRawValue)
    else { return }

    switch status {
    case .complete:
      src!.handleBuffer(sampleBuffer: sampleBuffer)
    case .idle:
      src!.handleIdle(pts: sampleBuffer.presentationTimeStamp)
    default:
      break
    }
  }

  public func stream(_ stream: SCStream, didStopWithError error: Error) {
    src?.handleStop(with: error)
  }
}

public protocol SCKitSrc: AnyObject {
  func handleBuffer(sampleBuffer: CMSampleBuffer)
  func handleIdle(pts: CMTime)
  func handleStop(with error: Error)
}
