/*
 * Copyright (c) 2014, Ericsson AB. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this
 * list of conditions and the following disclaimer in the documentation and/or other
 * materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 */

#ifndef gstdtlscommon_h
#define gstdtlscommon_h

#ifndef ER_DTLS_USE_GST_LOG
# define ER_DTLS_USE_GST_LOG 0
#endif

#if ER_DTLS_USE_GST_LOG
# include <gst/gst.h>
#endif

G_BEGIN_DECLS

#define UNUSED(param) while (0) { (void)(param); }

#if ER_DTLS_USE_GST_LOG
#   define LOG_ERROR(obj, ...) GST_ERROR_OBJECT(obj, __VA_ARGS__ )
#   define LOG_WARNING(obj, ...) GST_WARNING_OBJECT(obj, __VA_ARGS__ )
#   define LOG_FIXME(obj, ...) GST_FIXME_OBJECT(obj, __VA_ARGS__ )
#   define LOG_INFO(obj, ...) GST_INFO_OBJECT(obj, __VA_ARGS__ )
#   define LOG_DEBUG(obj, ...) GST_DEBUG_OBJECT(obj, __VA_ARGS__ )
#   define LOG_LOG(obj, ...) GST_LOG_OBJECT(obj, __VA_ARGS__ )
#   define LOG_TRACE(obj, ...) GST_TRACE_OBJECT(obj, __VA_ARGS__ )
#else
#   define LOG_ERROR(obj, fmt, ...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL, "%p: "fmt, obj, ##__VA_ARGS__)
#   define LOG_WARNING(obj, fmt, ...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "%p: "fmt, obj, ##__VA_ARGS__)
#   define LOG_FIXME(obj, fmt, ...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "%p: "fmt, obj, ##__VA_ARGS__)
#   define LOG_INFO(obj, fmt, ...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_MESSAGE, "%p: "fmt, obj, ##__VA_ARGS__)
#   define LOG_DEBUG(obj, fmt, ...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_INFO, "%p: "fmt, obj, ##__VA_ARGS__)
#   define LOG_LOG(obj, fmt, ...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%p: "fmt, obj, ##__VA_ARGS__)
#   define LOG_TRACE(obj, fmt, ...) g_log(G_LOG_DOMAIN, G_LOG_LEVEL_DEBUG, "%p: "fmt, obj, ##__VA_ARGS__)
#endif

G_END_DECLS

#endif /* gstdtlscommon_h */
