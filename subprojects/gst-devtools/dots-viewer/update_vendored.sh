#!/bin/bash
set -e

if ! command -v npm &> /dev/null; then
  echo "npm is not installed. Please install Node.js and npm first."
  exit 1
fi

echo "Installing dependencies..."
npm install

echo "Building bundles..."
npm run build
