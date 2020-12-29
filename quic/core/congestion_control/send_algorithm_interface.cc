// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "quic/core/congestion_control/send_algorithm_interface.h"

#include "absl/base/attributes.h"
#include "quic/core/congestion_control/bbr2_sender.h"
#include "quic/core/congestion_control/bbr_sender.h"
#include "quic/core/congestion_control/tcp_cubic_sender_bytes.h"
#include "quic/core/quic_packets.h"
#include "quic/platform/api/quic_bug_tracker.h"
#include "quic/platform/api/quic_flag_utils.h"
#include "quic/platform/api/quic_flags.h"
#include "quic/platform/api/quic_pcc_sender.h"

namespace quic {

class RttStats;

// Factory for send side congestion control algorithm.
SendAlgorithmInterface* SendAlgorithmInterface::Create(
    const QuicClock* clock,
    const RttStats* rtt_stats,
    const QuicUnackedPacketMap* unacked_packets,
    CongestionControlType congestion_control_type,
    QuicRandom* random,
    QuicConnectionStats* stats,
    QuicPacketCount initial_congestion_window,
    SendAlgorithmInterface* old_send_algorithm) {
  QuicPacketCount max_congestion_window =
      GetQuicFlag(FLAGS_quic_max_congestion_window);
  switch (congestion_control_type) {
    case kGoogCC:  // GoogCC is not supported by quic/core, fall back to BBR.
    case kBBR:
      return new BbrSender(clock->ApproximateNow(), rtt_stats, unacked_packets,
                           initial_congestion_window, max_congestion_window,
                           random, stats);
    case kBBRv2:
      return new Bbr2Sender(
          clock->ApproximateNow(), rtt_stats, unacked_packets,
          initial_congestion_window, max_congestion_window, random, stats,
          old_send_algorithm &&
                  old_send_algorithm->GetCongestionControlType() == kBBR
              ? static_cast<BbrSender*>(old_send_algorithm)
              : nullptr);
    case kPCC:
      // PCC is work has stalled, fall back to CUBIC instead.
      ABSL_FALLTHROUGH_INTENDED;
    case kCubicBytes:
      return new TcpCubicSenderBytes(
          clock, rtt_stats, false /* don't use Reno */,
          initial_congestion_window, max_congestion_window, stats);
    case kRenoBytes:
      return new TcpCubicSenderBytes(clock, rtt_stats, true /* use Reno */,
                                     initial_congestion_window,
                                     max_congestion_window, stats);
  }
  return nullptr;
}

}  // namespace quic
