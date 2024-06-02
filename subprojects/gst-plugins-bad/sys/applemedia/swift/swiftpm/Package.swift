// swift-tools-version: 5.10
// The swift-tools-version declares the minimum version of Swift required to build this package.

import CompilerPluginSupport
import PackageDescription

let package = Package(
  name: "applemedia-swiftpm",
  platforms: [
    // This is the minimum target needed for swift-syntax.
    // TODO: Raise the whole Gst target to 10.15?
    // In theory, the macro only runs on the builder machine, but if we add more things here...
    .macOS(.v10_15)
  ],
  products: [
    .executable(name: "GstSwiftMacros", targets: ["GstSwiftMacros"]),
    .library(name: "GstSwiftDeps", type: .static, targets: ["GstSwiftDeps"]),
  ],
  dependencies: [
    .package(
      url: "https://github.com/apple/swift-collections.git",
      .upToNextMinor(from: "1.1.4")  // or `.upToNextMajor
    ),
    .package(url: "https://github.com/apple/swift-syntax", from: "510.0.2"),
  ],
  targets: [
    // Macros have to be a separate target
    // Here, we will build macros with SwiftPM, but then import them in the Meson/ninja build
    // (as a compiler plugin!)
    .executableTarget(
      name: "GstSwiftMacros",
      dependencies: [
        .product(name: "SwiftSyntaxMacros", package: "swift-syntax"),
        .product(name: "SwiftCompilerPlugin", package: "swift-syntax"),
      ]
    ),
    // This only exists to make SwiftPM build dependencies which we will use from our Meson build
    .target(
      name: "GstSwiftDeps",
      dependencies: [
        .product(name: "Collections", package: "swift-collections")
      ],
      swiftSettings: [
        .interoperabilityMode(.C)
      ]
    ),
  ]
)
