/*
Copyright (c) 2015 - 2023 Advanced Micro Devices, Inc. All rights reserved.

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
*/

#pragma once

G_BEGIN_DECLS

typedef enum hiprtcResult {
    HIPRTC_SUCCESS = 0,   ///< Success
    HIPRTC_ERROR_OUT_OF_MEMORY = 1,  ///< Out of memory
    HIPRTC_ERROR_PROGRAM_CREATION_FAILURE = 2,   ///< Failed to create program
    HIPRTC_ERROR_INVALID_INPUT = 3,   ///< Invalid input
    HIPRTC_ERROR_INVALID_PROGRAM = 4,   ///< Invalid program
    HIPRTC_ERROR_INVALID_OPTION = 5,   ///< Invalid option
    HIPRTC_ERROR_COMPILATION = 6,   ///< Compilation error
    HIPRTC_ERROR_BUILTIN_OPERATION_FAILURE = 7,   ///< Failed in builtin operation
    HIPRTC_ERROR_NO_NAME_EXPRESSIONS_AFTER_COMPILATION = 8,   ///< No name expression after compilation
    HIPRTC_ERROR_NO_LOWERED_NAMES_BEFORE_COMPILATION = 9,   ///< No lowered names before compilation
    HIPRTC_ERROR_NAME_EXPRESSION_NOT_VALID = 10,   ///< Invalid name expression
    HIPRTC_ERROR_INTERNAL_ERROR = 11,   ///< Internal error
    HIPRTC_ERROR_LINKING = 100   ///< Error in linking
} hiprtcResult;

typedef struct _hiprtcProgram* hiprtcProgram;

G_END_DECLS