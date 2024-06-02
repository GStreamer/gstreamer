import Dispatch
import Foundation

/// Remember to not pass any conditionals into the closure!
/// (it will wait for nothing...)
func waitFor<T>(function: (@escaping (T?, Error?) -> Void) -> Void) throws -> T {
  let semaphore = DispatchSemaphore(value: 0)
  var result: T?
  var error: Error?

  function { response, err in
    result = response
    error = err
    semaphore.signal()
  }

  semaphore.wait()

  if let error = error {
    throw error
  }

  guard let unwrappedResult = result else {
    throw NSError(
      domain: "CompletionHandlerWrapperError", code: -1,
      userInfo: [NSLocalizedDescriptionKey: "Got null but no error, shouldn't happen!"])
  }

  return unwrappedResult
}

func waitFor(function: (@escaping (Error?) -> Void) -> Void) throws {
  let semaphore = DispatchSemaphore(value: 0)
  var error: Error?

  function { err in
    error = err
    semaphore.signal()
  }

  semaphore.wait()

  if let error = error {
    throw error
  }
}
