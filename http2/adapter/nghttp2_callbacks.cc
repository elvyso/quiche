#include "http2/adapter/nghttp2_callbacks.h"

#include <cstdint>
#include <cstring>

#include "absl/strings/string_view.h"
#include "http2/adapter/http2_protocol.h"
#include "http2/adapter/http2_visitor_interface.h"
#include "http2/adapter/nghttp2_data_provider.h"
#include "http2/adapter/nghttp2_util.h"
#include "third_party/nghttp2/nghttp2.h"
#include "third_party/nghttp2/src/lib/includes/nghttp2/nghttp2.h"
#include "common/platform/api/quiche_logging.h"
#include "common/quiche_endian.h"

namespace http2 {
namespace adapter {
namespace callbacks {

ssize_t OnReadyToSend(nghttp2_session* /* session */,
                      const uint8_t* data,
                      size_t length,
                      int flags,
                      void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  const ssize_t result = visitor->OnReadyToSend(ToStringView(data, length));
  if (result < 0) {
    return NGHTTP2_ERR_CALLBACK_FAILURE;
  } else if (result == 0) {
    return NGHTTP2_ERR_WOULDBLOCK;
  } else {
    return result;
  }
}

int OnBeginFrame(nghttp2_session* /* session */,
                 const nghttp2_frame_hd* header,
                 void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  visitor->OnFrameHeader(header->stream_id, header->length, header->type,
                         header->flags);
  if (header->type == NGHTTP2_DATA) {
    visitor->OnBeginDataForStream(header->stream_id, header->length);
  }
  return 0;
}

int OnFrameReceived(nghttp2_session* /* session */,
                    const nghttp2_frame* frame,
                    void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  const Http2StreamId stream_id = frame->hd.stream_id;
  switch (frame->hd.type) {
    // The beginning of the DATA frame is handled in OnBeginFrame(), and the
    // beginning of the header block is handled in client/server-specific
    // callbacks. This callback handles the point at which the entire logical
    // frame has been received and processed.
    case NGHTTP2_DATA:
      if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
        visitor->OnEndStream(stream_id);
      }
      break;
    case NGHTTP2_HEADERS: {
      if (frame->hd.flags & NGHTTP2_FLAG_END_HEADERS) {
        visitor->OnEndHeadersForStream(stream_id);
      }
      if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
        visitor->OnEndStream(stream_id);
      }
      break;
    }
    case NGHTTP2_PRIORITY: {
      nghttp2_priority_spec priority_spec = frame->priority.pri_spec;
      visitor->OnPriorityForStream(stream_id, priority_spec.stream_id,
                                   priority_spec.weight,
                                   priority_spec.exclusive != 0);
      break;
    }
    case NGHTTP2_RST_STREAM: {
      visitor->OnRstStream(stream_id,
                           ToHttp2ErrorCode(frame->rst_stream.error_code));
      break;
    }
    case NGHTTP2_SETTINGS:
      if (frame->hd.flags & NGHTTP2_FLAG_ACK) {
        visitor->OnSettingsAck();
      } else {
        visitor->OnSettingsStart();
        for (int i = 0; i < frame->settings.niv; ++i) {
          nghttp2_settings_entry entry = frame->settings.iv[i];
          // The nghttp2_settings_entry uses int32_t for the ID; we must cast.
          visitor->OnSetting(Http2Setting{
              .id = static_cast<Http2SettingsId>(entry.settings_id),
              .value = entry.value});
        }
        visitor->OnSettingsEnd();
      }
      break;
    case NGHTTP2_PUSH_PROMISE:
      // This case is handled by headers-related callbacks:
      //   1. visitor->OnPushPromiseForStream() is invoked in the client-side
      //      OnHeadersStart() adapter callback, as nghttp2 only allows clients
      //      to receive PUSH_PROMISE frames.
      //   2. visitor->OnHeaderForStream() is invoked for each server push
      //      request header in the PUSH_PROMISE header block.
      //   3. This switch statement is reached once all server push request
      //      headers have been parsed.
      break;
    case NGHTTP2_PING: {
      Http2PingId ping_id;
      std::memcpy(&ping_id, frame->ping.opaque_data, sizeof(Http2PingId));
      visitor->OnPing(quiche::QuicheEndian::NetToHost64(ping_id),
                      (frame->hd.flags & NGHTTP2_FLAG_ACK) != 0);
      break;
    }
    case NGHTTP2_GOAWAY: {
      absl::string_view opaque_data(
          reinterpret_cast<const char*>(frame->goaway.opaque_data),
          frame->goaway.opaque_data_len);
      visitor->OnGoAway(frame->goaway.last_stream_id,
                        ToHttp2ErrorCode(frame->goaway.error_code),
                        opaque_data);
      break;
    }
    case NGHTTP2_WINDOW_UPDATE: {
      visitor->OnWindowUpdate(stream_id,
                              frame->window_update.window_size_increment);
      break;
    }
    case NGHTTP2_CONTINUATION:
      // This frame type should not be passed to any callbacks, according to
      // https://nghttp2.org/documentation/enums.html#c.NGHTTP2_CONTINUATION.
      QUICHE_LOG(ERROR) << "Unexpected receipt of NGHTTP2_CONTINUATION type!";
      break;
    case NGHTTP2_ALTSVC:
      break;
    case NGHTTP2_ORIGIN:
      break;
  }

  return 0;
}

int OnBeginHeaders(nghttp2_session* /* session */,
                   const nghttp2_frame* frame,
                   void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  visitor->OnBeginHeadersForStream(frame->hd.stream_id);
  return 0;
}

int OnHeader(nghttp2_session* /* session */,
             const nghttp2_frame* frame,
             nghttp2_rcbuf* name,
             nghttp2_rcbuf* value,
             uint8_t flags,
             void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  const bool success = visitor->OnHeaderForStream(
      frame->hd.stream_id, ToStringView(name), ToStringView(value));
  return success ? 0 : NGHTTP2_ERR_HTTP_HEADER;
}

int OnBeforeFrameSent(nghttp2_session* /* session */,
                      const nghttp2_frame* frame, void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  return visitor->OnBeforeFrameSent(frame->hd.type, frame->hd.stream_id,
                                    frame->hd.length, frame->hd.flags);
}

int OnFrameSent(nghttp2_session* /* session */, const nghttp2_frame* frame,
                void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  uint32_t error_code = 0;
  if (frame->hd.type == NGHTTP2_RST_STREAM) {
    error_code = frame->rst_stream.error_code;
  } else if (frame->hd.type == NGHTTP2_GOAWAY) {
    error_code = frame->goaway.error_code;
  }
  return visitor->OnFrameSent(frame->hd.type, frame->hd.stream_id,
                              frame->hd.length, frame->hd.flags, error_code);
}

int OnInvalidFrameReceived(nghttp2_session* /* session */,
                           const nghttp2_frame* frame, int lib_error_code,
                           void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  const bool result =
      visitor->OnInvalidFrame(frame->hd.stream_id, lib_error_code);
  return result ? 0 : NGHTTP2_ERR_CALLBACK_FAILURE;
}

int OnDataChunk(nghttp2_session* /* session */,
                uint8_t flags,
                Http2StreamId stream_id,
                const uint8_t* data,
                size_t len,
                void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  visitor->OnDataForStream(
      stream_id, absl::string_view(reinterpret_cast<const char*>(data), len));
  return 0;
}

int OnStreamClosed(nghttp2_session* /* session */,
                   Http2StreamId stream_id,
                   uint32_t error_code,
                   void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  visitor->OnCloseStream(stream_id, ToHttp2ErrorCode(error_code));
  return 0;
}

int OnExtensionChunkReceived(nghttp2_session* session,
                             const nghttp2_frame_hd* hd, const uint8_t* data,
                             size_t len, void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  if (hd->type != kMetadataFrameType) {
    QUICHE_LOG(ERROR) << "Unexpected frame type: "
                      << static_cast<int>(hd->type);
    return NGHTTP2_ERR_CANCEL;
  }
  visitor->OnMetadataForStream(hd->stream_id, ToStringView(data, len));
  return 0;
}

int OnUnpackExtensionCallback(nghttp2_session* session, void** payload,
                              const nghttp2_frame_hd* hd, void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  if (hd->flags == kMetadataEndFlag) {
    const bool result = visitor->OnMetadataEndForStream(hd->stream_id);
    if (!result) {
      return NGHTTP2_ERR_CALLBACK_FAILURE;
    }
  }
  return 0;
}

ssize_t OnPackExtensionCallback(nghttp2_session* session, uint8_t* buf,
                                size_t len, const nghttp2_frame* frame,
                                void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  ssize_t written = 0;
  visitor->OnReadyToSendMetadataForStream(
      frame->hd.stream_id, reinterpret_cast<char*>(buf), len, &written);
  return written;
}

int OnError(nghttp2_session* session, int lib_error_code, const char* msg,
            size_t len, void* user_data) {
  QUICHE_CHECK_NE(user_data, nullptr);
  auto* visitor = static_cast<Http2VisitorInterface*>(user_data);
  visitor->OnErrorDebug(absl::string_view(msg, len));
  return 0;
}

nghttp2_session_callbacks_unique_ptr Create() {
  nghttp2_session_callbacks* callbacks;
  nghttp2_session_callbacks_new(&callbacks);

  nghttp2_session_callbacks_set_send_callback(callbacks, &OnReadyToSend);
  nghttp2_session_callbacks_set_on_begin_frame_callback(callbacks,
                                                        &OnBeginFrame);
  nghttp2_session_callbacks_set_on_frame_recv_callback(callbacks,
                                                       &OnFrameReceived);
  nghttp2_session_callbacks_set_on_begin_headers_callback(callbacks,
                                                          &OnBeginHeaders);
  nghttp2_session_callbacks_set_on_header_callback2(callbacks, &OnHeader);
  nghttp2_session_callbacks_set_on_data_chunk_recv_callback(callbacks,
                                                            &OnDataChunk);
  nghttp2_session_callbacks_set_on_stream_close_callback(callbacks,
                                                         &OnStreamClosed);
  nghttp2_session_callbacks_set_before_frame_send_callback(callbacks,
                                                           &OnBeforeFrameSent);
  nghttp2_session_callbacks_set_on_frame_send_callback(callbacks, &OnFrameSent);
  nghttp2_session_callbacks_set_on_invalid_frame_recv_callback(
      callbacks, &OnInvalidFrameReceived);
  nghttp2_session_callbacks_set_error_callback2(callbacks, &OnError);
  // on_frame_not_send_callback <- just ignored
  nghttp2_session_callbacks_set_send_data_callback(
      callbacks, &DataFrameSourceSendCallback);
  nghttp2_session_callbacks_set_pack_extension_callback(
      callbacks, &OnPackExtensionCallback);
  nghttp2_session_callbacks_set_unpack_extension_callback(
      callbacks, &OnUnpackExtensionCallback);
  nghttp2_session_callbacks_set_on_extension_chunk_recv_callback(
      callbacks, &OnExtensionChunkReceived);
  return MakeCallbacksPtr(callbacks);
}

}  // namespace callbacks
}  // namespace adapter
}  // namespace http2
