// Copyright (c) 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/quic/congestion_control/tcp_cubic_sender_bytes.h"

#include <algorithm>
#include <cstdint>
#include <memory>

#include "base/logging.h"
#include "net/quic/congestion_control/rtt_stats.h"
#include "net/quic/crypto/crypto_protocol.h"
#include "net/quic/proto/cached_network_parameters.pb.h"
#include "net/quic/quic_flags.h"
#include "net/quic/quic_protocol.h"
#include "net/quic/quic_utils.h"
#include "net/quic/test_tools/mock_clock.h"
#include "net/quic/test_tools/quic_config_peer.h"
#include "net/quic/test_tools/quic_test_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {
namespace test {

// TODO(ianswett): A number of theses tests were written with the assumption of
// an initial CWND of 10. They have carefully calculated values which should be
// updated to be based on kInitialCongestionWindow.
const uint32_t kInitialCongestionWindowPackets = 10;
const uint32_t kDefaultWindowTCP =
    kInitialCongestionWindowPackets * kDefaultTCPMSS;
const float kRenoBeta = 0.7f;  // Reno backoff factor.

class TcpCubicSenderBytesPeer : public TcpCubicSenderBytes {
 public:
  TcpCubicSenderBytesPeer(const QuicClock* clock, bool reno)
      : TcpCubicSenderBytes(clock,
                            &rtt_stats_,
                            reno,
                            kInitialCongestionWindowPackets,
                            kMaxCongestionWindow,
                            &stats_) {}

  const HybridSlowStart& hybrid_slow_start() const {
    return hybrid_slow_start_;
  }

  float GetRenoBeta() const { return RenoBeta(); }

  RttStats rtt_stats_;
  QuicConnectionStats stats_;
};

class TcpCubicSenderBytesTest : public ::testing::Test {
 protected:
  TcpCubicSenderBytesTest()
      : one_ms_(QuicTime::Delta::FromMilliseconds(1)),
        sender_(new TcpCubicSenderBytesPeer(&clock_, true)),
        packet_number_(1),
        acked_packet_number_(0),
        bytes_in_flight_(0) {}

  int SendAvailableSendWindow() {
    return SendAvailableSendWindow(kDefaultTCPMSS);
  }

  int SendAvailableSendWindow(QuicPacketLength packet_length) {
    // Send as long as TimeUntilSend returns Zero.
    int packets_sent = 0;
    bool can_send =
        sender_->TimeUntilSend(clock_.Now(), bytes_in_flight_).IsZero();
    while (can_send) {
      sender_->OnPacketSent(clock_.Now(), bytes_in_flight_, packet_number_++,
                            kDefaultTCPMSS, HAS_RETRANSMITTABLE_DATA);
      ++packets_sent;
      bytes_in_flight_ += kDefaultTCPMSS;
      can_send =
          sender_->TimeUntilSend(clock_.Now(), bytes_in_flight_).IsZero();
    }
    return packets_sent;
  }

  // Normal is that TCP acks every other segment.
  void AckNPackets(int n) {
    sender_->rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(60),
                                  QuicTime::Delta::Zero(), clock_.Now());
    SendAlgorithmInterface::CongestionVector acked_packets;
    SendAlgorithmInterface::CongestionVector lost_packets;
    for (int i = 0; i < n; ++i) {
      ++acked_packet_number_;
      acked_packets.push_back(
          std::make_pair(acked_packet_number_, kDefaultTCPMSS));
    }
    sender_->OnCongestionEvent(true, bytes_in_flight_, acked_packets,
                               lost_packets);
    bytes_in_flight_ -= n * kDefaultTCPMSS;
    clock_.AdvanceTime(one_ms_);
  }

  void LoseNPackets(int n) { LoseNPackets(n, kDefaultTCPMSS); }

  void LoseNPackets(int n, QuicPacketLength packet_length) {
    SendAlgorithmInterface::CongestionVector acked_packets;
    SendAlgorithmInterface::CongestionVector lost_packets;
    for (int i = 0; i < n; ++i) {
      ++acked_packet_number_;
      lost_packets.push_back(
          std::make_pair(acked_packet_number_, packet_length));
    }
    sender_->OnCongestionEvent(false, bytes_in_flight_, acked_packets,
                               lost_packets);
    bytes_in_flight_ -= n * packet_length;
  }

  // Does not increment acked_packet_number_.
  void LosePacket(QuicPacketNumber packet_number) {
    SendAlgorithmInterface::CongestionVector acked_packets;
    SendAlgorithmInterface::CongestionVector lost_packets;
    lost_packets.push_back(std::make_pair(packet_number, kDefaultTCPMSS));
    sender_->OnCongestionEvent(false, bytes_in_flight_, acked_packets,
                               lost_packets);
    bytes_in_flight_ -= kDefaultTCPMSS;
  }

  const QuicTime::Delta one_ms_;
  MockClock clock_;
  std::unique_ptr<TcpCubicSenderBytesPeer> sender_;
  QuicPacketNumber packet_number_;
  QuicPacketNumber acked_packet_number_;
  QuicByteCount bytes_in_flight_;
};

TEST_F(TcpCubicSenderBytesTest, SimpleSender) {
  // At startup make sure we are at the default.
  EXPECT_EQ(kDefaultWindowTCP, sender_->GetCongestionWindow());
  // At startup make sure we can send.
  EXPECT_TRUE(sender_->TimeUntilSend(clock_.Now(), 0).IsZero());
  // Make sure we can send.
  EXPECT_TRUE(sender_->TimeUntilSend(clock_.Now(), 0).IsZero());
  // And that window is un-affected.
  EXPECT_EQ(kDefaultWindowTCP, sender_->GetCongestionWindow());

  // Fill the send window with data, then verify that we can't send.
  SendAvailableSendWindow();
  EXPECT_FALSE(
      sender_->TimeUntilSend(clock_.Now(), sender_->GetCongestionWindow())
          .IsZero());
}

TEST_F(TcpCubicSenderBytesTest, ApplicationLimitedSlowStart) {
  // Send exactly 10 packets and ensure the CWND ends at 14 packets.
  const int kNumberOfAcks = 5;
  // At startup make sure we can send.
  EXPECT_TRUE(sender_->TimeUntilSend(clock_.Now(), 0).IsZero());
  // Make sure we can send.
  EXPECT_TRUE(sender_->TimeUntilSend(clock_.Now(), 0).IsZero());

  SendAvailableSendWindow();
  for (int i = 0; i < kNumberOfAcks; ++i) {
    AckNPackets(2);
  }
  QuicByteCount bytes_to_send = sender_->GetCongestionWindow();
  // It's expected 2 acks will arrive when the bytes_in_flight are greater than
  // half the CWND.
  EXPECT_EQ(kDefaultWindowTCP + kDefaultTCPMSS * 2 * 2, bytes_to_send);
}

TEST_F(TcpCubicSenderBytesTest, ExponentialSlowStart) {
  const int kNumberOfAcks = 20;
  // At startup make sure we can send.
  EXPECT_TRUE(sender_->TimeUntilSend(clock_.Now(), 0).IsZero());
  EXPECT_EQ(QuicBandwidth::Zero(), sender_->BandwidthEstimate());
  // Make sure we can send.
  EXPECT_TRUE(sender_->TimeUntilSend(clock_.Now(), 0).IsZero());

  for (int i = 0; i < kNumberOfAcks; ++i) {
    // Send our full send window.
    SendAvailableSendWindow();
    AckNPackets(2);
  }
  const QuicByteCount cwnd = sender_->GetCongestionWindow();
  EXPECT_EQ(kDefaultWindowTCP + kDefaultTCPMSS * 2 * kNumberOfAcks, cwnd);
  EXPECT_EQ(QuicBandwidth::FromBytesAndTimeDelta(
                cwnd, sender_->rtt_stats_.smoothed_rtt()),
            sender_->BandwidthEstimate());
}

TEST_F(TcpCubicSenderBytesTest, SlowStartPacketLoss) {
  sender_->SetNumEmulatedConnections(1);
  const int kNumberOfAcks = 10;
  for (int i = 0; i < kNumberOfAcks; ++i) {
    // Send our full send window.
    SendAvailableSendWindow();
    AckNPackets(2);
  }
  SendAvailableSendWindow();
  QuicByteCount expected_send_window =
      kDefaultWindowTCP + (kDefaultTCPMSS * 2 * kNumberOfAcks);
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Lose a packet to exit slow start.
  LoseNPackets(1);
  size_t packets_in_recovery_window = expected_send_window / kDefaultTCPMSS;

  // We should now have fallen out of slow start with a reduced window.
  expected_send_window *= kRenoBeta;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Recovery phase. We need to ack every packet in the recovery window before
  // we exit recovery.
  size_t number_of_packets_in_window = expected_send_window / kDefaultTCPMSS;
  DVLOG(1) << "number_packets: " << number_of_packets_in_window;
  AckNPackets(packets_in_recovery_window);
  SendAvailableSendWindow();
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // We need to ack an entire window before we increase CWND by 1.
  AckNPackets(number_of_packets_in_window - 2);
  SendAvailableSendWindow();
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Next ack should increase cwnd by 1.
  AckNPackets(1);
  expected_send_window += kDefaultTCPMSS;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Now RTO and ensure slow start gets reset.
  EXPECT_TRUE(sender_->hybrid_slow_start().started());
  sender_->OnRetransmissionTimeout(true);
  EXPECT_FALSE(sender_->hybrid_slow_start().started());
}

TEST_F(TcpCubicSenderBytesTest, SlowStartPacketLossWithLargeReduction) {
  FLAGS_quic_sslr_limit_reduction = true;
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kSSLR);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  sender_->SetFromConfig(config, Perspective::IS_SERVER);

  sender_->SetNumEmulatedConnections(1);
  const int kNumberOfAcks = (kDefaultWindowTCP / (2 * kDefaultTCPMSS)) - 1;
  for (int i = 0; i < kNumberOfAcks; ++i) {
    // Send our full send window.
    SendAvailableSendWindow();
    AckNPackets(2);
  }
  SendAvailableSendWindow();
  QuicByteCount expected_send_window =
      kDefaultWindowTCP + (kDefaultTCPMSS * 2 * kNumberOfAcks);
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Lose a packet to exit slow start. We should now have fallen out of
  // slow start with a window reduced by 1.
  LoseNPackets(1);
  expected_send_window -= kDefaultTCPMSS;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Lose 5 packets in recovery and verify that congestion window is reduced
  // further.
  LoseNPackets(5);
  expected_send_window -= 5 * kDefaultTCPMSS;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());
  // Lose another 10 packets and ensure it reduces below half the peak CWND,
  // because we never acked the full IW.
  LoseNPackets(10);
  expected_send_window -= 10 * kDefaultTCPMSS;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  size_t packets_in_recovery_window = expected_send_window / kDefaultTCPMSS;

  // Recovery phase. We need to ack every packet in the recovery window before
  // we exit recovery.
  size_t number_of_packets_in_window = expected_send_window / kDefaultTCPMSS;
  DVLOG(1) << "number_packets: " << number_of_packets_in_window;
  AckNPackets(packets_in_recovery_window);
  SendAvailableSendWindow();
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // We need to ack an entire window before we increase CWND by 1.
  AckNPackets(number_of_packets_in_window - 1);
  SendAvailableSendWindow();
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Next ack should increase cwnd by 1.
  AckNPackets(1);
  expected_send_window += kDefaultTCPMSS;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Now RTO and ensure slow start gets reset.
  EXPECT_TRUE(sender_->hybrid_slow_start().started());
  sender_->OnRetransmissionTimeout(true);
  EXPECT_FALSE(sender_->hybrid_slow_start().started());
}

TEST_F(TcpCubicSenderBytesTest, SlowStartHalfPacketLossWithLargeReduction) {
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kSSLR);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  sender_->SetFromConfig(config, Perspective::IS_SERVER);

  sender_->SetNumEmulatedConnections(1);
  const int kNumberOfAcks = 10;
  for (int i = 0; i < kNumberOfAcks; ++i) {
    // Send our full send window in half sized packets.
    SendAvailableSendWindow(kDefaultTCPMSS / 2);
    AckNPackets(2);
  }
  SendAvailableSendWindow(kDefaultTCPMSS / 2);
  QuicByteCount expected_send_window =
      kDefaultWindowTCP + (kDefaultTCPMSS * 2 * kNumberOfAcks);
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Lose a packet to exit slow start. We should now have fallen out of
  // slow start with a window reduced by 1.
  LoseNPackets(1);
  expected_send_window -= kDefaultTCPMSS;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Lose 10 packets in recovery and verify that congestion window is reduced
  // by 5 packets.
  LoseNPackets(10, kDefaultTCPMSS / 2);
  expected_send_window -= 5 * kDefaultTCPMSS;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());
}

TEST_F(TcpCubicSenderBytesTest, SlowStartPacketLossWithMaxHalfReduction) {
  FLAGS_quic_sslr_limit_reduction = true;
  QuicConfig config;
  QuicTagVector options;
  options.push_back(kSSLR);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  sender_->SetFromConfig(config, Perspective::IS_SERVER);

  sender_->SetNumEmulatedConnections(1);
  const int kNumberOfAcks = kInitialCongestionWindowPackets / 2;
  for (int i = 0; i < kNumberOfAcks; ++i) {
    // Send our full send window.
    SendAvailableSendWindow();
    AckNPackets(2);
  }
  SendAvailableSendWindow();
  QuicByteCount expected_send_window =
      kDefaultWindowTCP + (kDefaultTCPMSS * 2 * kNumberOfAcks);
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Lose a packet to exit slow start. We should now have fallen out of
  // slow start with a window reduced by 1.
  LoseNPackets(1);
  expected_send_window -= kDefaultTCPMSS;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Lose half the outstanding packets in recovery and verify the congestion
  // window is only reduced by a max of half.
  LoseNPackets(kNumberOfAcks * 2);
  expected_send_window -= (kNumberOfAcks * 2 - 1) * kDefaultTCPMSS;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());
  LoseNPackets(5);
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());
}

TEST_F(TcpCubicSenderBytesTest, NoPRRWhenLessThanOnePacketInFlight) {
  SendAvailableSendWindow();
  LoseNPackets(kInitialCongestionWindowPackets - 1);
  AckNPackets(1);
  // PRR will allow 2 packets for every ack during recovery.
  EXPECT_EQ(2, SendAvailableSendWindow());
  // Simulate abandoning all packets by supplying a bytes_in_flight of 0.
  // PRR should now allow a packet to be sent, even though prr's state variables
  // believe it has sent enough packets.
  EXPECT_EQ(QuicTime::Delta::Zero(), sender_->TimeUntilSend(clock_.Now(), 0));
}

TEST_F(TcpCubicSenderBytesTest, SlowStartPacketLossPRR) {
  sender_->SetNumEmulatedConnections(1);
  // Test based on the first example in RFC6937.
  // Ack 10 packets in 5 acks to raise the CWND to 20, as in the example.
  const int kNumberOfAcks = 5;
  for (int i = 0; i < kNumberOfAcks; ++i) {
    // Send our full send window.
    SendAvailableSendWindow();
    AckNPackets(2);
  }
  SendAvailableSendWindow();
  QuicByteCount expected_send_window =
      kDefaultWindowTCP + (kDefaultTCPMSS * 2 * kNumberOfAcks);
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  LoseNPackets(1);

  // We should now have fallen out of slow start with a reduced window.
  size_t send_window_before_loss = expected_send_window;
  expected_send_window *= kRenoBeta;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Testing TCP proportional rate reduction.
  // We should send packets paced over the received acks for the remaining
  // outstanding packets. The number of packets before we exit recovery is the
  // original CWND minus the packet that has been lost and the one which
  // triggered the loss.
  size_t remaining_packets_in_recovery =
      send_window_before_loss / kDefaultTCPMSS - 2;

  for (size_t i = 0; i < remaining_packets_in_recovery; ++i) {
    AckNPackets(1);
    SendAvailableSendWindow();
    EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());
  }

  // We need to ack another window before we increase CWND by 1.
  size_t number_of_packets_in_window = expected_send_window / kDefaultTCPMSS;
  for (size_t i = 0; i < number_of_packets_in_window; ++i) {
    AckNPackets(1);
    EXPECT_EQ(1, SendAvailableSendWindow());
    EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());
  }

  AckNPackets(1);
  expected_send_window += kDefaultTCPMSS;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());
}

TEST_F(TcpCubicSenderBytesTest, SlowStartBurstPacketLossPRR) {
  sender_->SetNumEmulatedConnections(1);
  // Test based on the second example in RFC6937, though we also implement
  // forward acknowledgements, so the first two incoming acks will trigger
  // PRR immediately.
  // Ack 20 packets in 10 acks to raise the CWND to 30.
  const int kNumberOfAcks = 10;
  for (int i = 0; i < kNumberOfAcks; ++i) {
    // Send our full send window.
    SendAvailableSendWindow();
    AckNPackets(2);
  }
  SendAvailableSendWindow();
  QuicByteCount expected_send_window =
      kDefaultWindowTCP + (kDefaultTCPMSS * 2 * kNumberOfAcks);
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Lose one more than the congestion window reduction, so that after loss,
  // bytes_in_flight is lesser than the congestion window.
  size_t send_window_after_loss = kRenoBeta * expected_send_window;
  size_t num_packets_to_lose =
      (expected_send_window - send_window_after_loss) / kDefaultTCPMSS + 1;
  LoseNPackets(num_packets_to_lose);
  // Immediately after the loss, ensure at least one packet can be sent.
  // Losses without subsequent acks can occur with timer based loss detection.
  EXPECT_TRUE(sender_->TimeUntilSend(clock_.Now(), bytes_in_flight_).IsZero());
  AckNPackets(1);

  // We should now have fallen out of slow start with a reduced window.
  expected_send_window *= kRenoBeta;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Only 2 packets should be allowed to be sent, per PRR-SSRB.
  EXPECT_EQ(2, SendAvailableSendWindow());

  // Ack the next packet, which triggers another loss.
  LoseNPackets(1);
  AckNPackets(1);

  // Send 2 packets to simulate PRR-SSRB.
  EXPECT_EQ(2, SendAvailableSendWindow());

  // Ack the next packet, which triggers another loss.
  LoseNPackets(1);
  AckNPackets(1);

  // Send 2 packets to simulate PRR-SSRB.
  EXPECT_EQ(2, SendAvailableSendWindow());

  // Exit recovery and return to sending at the new rate.
  for (int i = 0; i < kNumberOfAcks; ++i) {
    AckNPackets(1);
    EXPECT_EQ(1, SendAvailableSendWindow());
  }
}

TEST_F(TcpCubicSenderBytesTest, RTOCongestionWindow) {
  EXPECT_EQ(kDefaultWindowTCP, sender_->GetCongestionWindow());
  // Expect the window to decrease to the minimum once the RTO fires and slow
  // start threshold to be set to 1/2 of the CWND.
  sender_->OnRetransmissionTimeout(true);
  EXPECT_EQ(2 * kDefaultTCPMSS, sender_->GetCongestionWindow());
  EXPECT_EQ(5u * kDefaultTCPMSS, sender_->GetSlowStartThreshold());
}

TEST_F(TcpCubicSenderBytesTest, RTOCongestionWindowNoRetransmission) {
  EXPECT_EQ(kDefaultWindowTCP, sender_->GetCongestionWindow());

  // Expect the window to remain unchanged if the RTO fires but no packets are
  // retransmitted.
  sender_->OnRetransmissionTimeout(false);
  EXPECT_EQ(kDefaultWindowTCP, sender_->GetCongestionWindow());
}

TEST_F(TcpCubicSenderBytesTest, RetransmissionDelay) {
  const int64_t kRttMs = 10;
  const int64_t kDeviationMs = 3;
  EXPECT_EQ(QuicTime::Delta::Zero(), sender_->RetransmissionDelay());

  sender_->rtt_stats_.UpdateRtt(QuicTime::Delta::FromMilliseconds(kRttMs),
                                QuicTime::Delta::Zero(), clock_.Now());

  // Initial value is to set the median deviation to half of the initial rtt,
  // the median in then multiplied by a factor of 4 and finally the smoothed rtt
  // is added which is the initial rtt.
  QuicTime::Delta expected_delay =
      QuicTime::Delta::FromMilliseconds(kRttMs + kRttMs / 2 * 4);
  EXPECT_EQ(expected_delay, sender_->RetransmissionDelay());

  for (int i = 0; i < 100; ++i) {
    // Run to make sure that we converge.
    sender_->rtt_stats_.UpdateRtt(
        QuicTime::Delta::FromMilliseconds(kRttMs + kDeviationMs),
        QuicTime::Delta::Zero(), clock_.Now());
    sender_->rtt_stats_.UpdateRtt(
        QuicTime::Delta::FromMilliseconds(kRttMs - kDeviationMs),
        QuicTime::Delta::Zero(), clock_.Now());
  }
  expected_delay = QuicTime::Delta::FromMilliseconds(kRttMs + kDeviationMs * 4);

  EXPECT_NEAR(kRttMs, sender_->rtt_stats_.smoothed_rtt().ToMilliseconds(), 1);
  EXPECT_NEAR(expected_delay.ToMilliseconds(),
              sender_->RetransmissionDelay().ToMilliseconds(), 1);
  EXPECT_EQ(static_cast<int64_t>(
                sender_->GetCongestionWindow() * kNumMicrosPerSecond /
                sender_->rtt_stats_.smoothed_rtt().ToMicroseconds()),
            sender_->BandwidthEstimate().ToBytesPerSecond());
}

TEST_F(TcpCubicSenderBytesTest, TcpCubicResetEpochOnQuiescence) {
  const int kMaxCongestionWindow = 50;
  const QuicByteCount kMaxCongestionWindowBytes =
      kMaxCongestionWindow * kDefaultTCPMSS;
  int num_sent = SendAvailableSendWindow();

  // Make sure we fall out of slow start.
  QuicByteCount saved_cwnd = sender_->GetCongestionWindow();
  LoseNPackets(1);
  EXPECT_GT(saved_cwnd, sender_->GetCongestionWindow());

  // Ack the rest of the outstanding packets to get out of recovery.
  for (int i = 1; i < num_sent; ++i) {
    AckNPackets(1);
  }
  EXPECT_EQ(0u, bytes_in_flight_);

  // Send a new window of data and ack all; cubic growth should occur.
  saved_cwnd = sender_->GetCongestionWindow();
  num_sent = SendAvailableSendWindow();
  for (int i = 0; i < num_sent; ++i) {
    AckNPackets(1);
  }
  EXPECT_LT(saved_cwnd, sender_->GetCongestionWindow());
  EXPECT_GT(kMaxCongestionWindowBytes, sender_->GetCongestionWindow());
  EXPECT_EQ(0u, bytes_in_flight_);

  // Quiescent time of 100 seconds
  clock_.AdvanceTime(QuicTime::Delta::FromMilliseconds(100000));

  // Send new window of data and ack one packet. Cubic epoch should have
  // been reset; ensure cwnd increase is not dramatic.
  saved_cwnd = sender_->GetCongestionWindow();
  SendAvailableSendWindow();
  AckNPackets(1);
  EXPECT_NEAR(saved_cwnd, sender_->GetCongestionWindow(), kDefaultTCPMSS);
  EXPECT_GT(kMaxCongestionWindowBytes, sender_->GetCongestionWindow());
}

TEST_F(TcpCubicSenderBytesTest, MultipleLossesInOneWindow) {
  SendAvailableSendWindow();
  const QuicByteCount initial_window = sender_->GetCongestionWindow();
  LosePacket(acked_packet_number_ + 1);
  const QuicByteCount post_loss_window = sender_->GetCongestionWindow();
  EXPECT_GT(initial_window, post_loss_window);
  LosePacket(acked_packet_number_ + 3);
  EXPECT_EQ(post_loss_window, sender_->GetCongestionWindow());
  LosePacket(packet_number_ - 1);
  EXPECT_EQ(post_loss_window, sender_->GetCongestionWindow());

  // Lose a later packet and ensure the window decreases.
  LosePacket(packet_number_);
  EXPECT_GT(post_loss_window, sender_->GetCongestionWindow());
}

TEST_F(TcpCubicSenderBytesTest, DontTrackAckPackets) {
  // Send a packet with no retransmittable data, and ensure it's not tracked.
  EXPECT_FALSE(sender_->OnPacketSent(clock_.Now(), bytes_in_flight_,
                                     packet_number_++, kDefaultTCPMSS,
                                     NO_RETRANSMITTABLE_DATA));

  // Send a data packet with retransmittable data, and ensure it is tracked.
  EXPECT_TRUE(sender_->OnPacketSent(clock_.Now(), bytes_in_flight_,
                                    packet_number_++, kDefaultTCPMSS,
                                    HAS_RETRANSMITTABLE_DATA));
}

TEST_F(TcpCubicSenderBytesTest, ConfigureMaxInitialWindow) {
  QuicConfig config;

  // Verify that kCOPT: kIW10 forces the congestion window to the default of 10.
  QuicTagVector options;
  options.push_back(kIW10);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  sender_->SetFromConfig(config, Perspective::IS_SERVER);
  EXPECT_EQ(10u * kDefaultTCPMSS, sender_->GetCongestionWindow());
}

TEST_F(TcpCubicSenderBytesTest, 2ConnectionCongestionAvoidanceAtEndOfRecovery) {
  sender_->SetNumEmulatedConnections(2);
  // Ack 10 packets in 5 acks to raise the CWND to 20.
  const int kNumberOfAcks = 5;
  for (int i = 0; i < kNumberOfAcks; ++i) {
    // Send our full send window.
    SendAvailableSendWindow();
    AckNPackets(2);
  }
  SendAvailableSendWindow();
  QuicByteCount expected_send_window =
      kDefaultWindowTCP + (kDefaultTCPMSS * 2 * kNumberOfAcks);
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  LoseNPackets(1);

  // We should now have fallen out of slow start with a reduced window.
  expected_send_window = expected_send_window * sender_->GetRenoBeta();
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // No congestion window growth should occur in recovery phase, i.e., until the
  // currently outstanding 20 packets are acked.
  for (int i = 0; i < 10; ++i) {
    // Send our full send window.
    SendAvailableSendWindow();
    EXPECT_TRUE(sender_->InRecovery());
    AckNPackets(2);
    EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());
  }
  EXPECT_FALSE(sender_->InRecovery());

  // Out of recovery now. Congestion window should not grow for half an RTT.
  size_t packets_in_send_window = expected_send_window / kDefaultTCPMSS;
  SendAvailableSendWindow();
  AckNPackets(packets_in_send_window / 2 - 2);
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Next ack should increase congestion window by 1MSS.
  SendAvailableSendWindow();
  AckNPackets(2);
  expected_send_window += kDefaultTCPMSS;
  packets_in_send_window += 1;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Congestion window should remain steady again for half an RTT.
  SendAvailableSendWindow();
  AckNPackets(packets_in_send_window / 2 - 1);
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Next ack should cause congestion window to grow by 1MSS.
  SendAvailableSendWindow();
  AckNPackets(2);
  expected_send_window += kDefaultTCPMSS;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());
}

TEST_F(TcpCubicSenderBytesTest, 1ConnectionCongestionAvoidanceAtEndOfRecovery) {
  sender_->SetNumEmulatedConnections(1);
  // Ack 10 packets in 5 acks to raise the CWND to 20.
  const int kNumberOfAcks = 5;
  for (int i = 0; i < kNumberOfAcks; ++i) {
    // Send our full send window.
    SendAvailableSendWindow();
    AckNPackets(2);
  }
  SendAvailableSendWindow();
  QuicByteCount expected_send_window =
      kDefaultWindowTCP + (kDefaultTCPMSS * 2 * kNumberOfAcks);
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  LoseNPackets(1);

  // We should now have fallen out of slow start with a reduced window.
  expected_send_window *= kRenoBeta;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // No congestion window growth should occur in recovery phase, i.e., until the
  // currently outstanding 20 packets are acked.
  for (int i = 0; i < 10; ++i) {
    // Send our full send window.
    SendAvailableSendWindow();
    EXPECT_TRUE(sender_->InRecovery());
    AckNPackets(2);
    EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());
  }
  EXPECT_FALSE(sender_->InRecovery());

  // Out of recovery now. Congestion window should not grow during RTT.
  for (uint64_t i = 0; i < expected_send_window / kDefaultTCPMSS - 2; i += 2) {
    // Send our full send window.
    SendAvailableSendWindow();
    AckNPackets(2);
    EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());
  }

  // Next ack should cause congestion window to grow by 1MSS.
  SendAvailableSendWindow();
  AckNPackets(2);
  expected_send_window += kDefaultTCPMSS;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());
}

TEST_F(TcpCubicSenderBytesTest, BandwidthResumption) {
  // Test that when provided with CachedNetworkParameters and opted in to the
  // bandwidth resumption experiment, that the TcpCubicSender sets initial CWND
  // appropriately.

  // Set some common values.
  CachedNetworkParameters cached_network_params;
  const QuicPacketCount kNumberOfPackets = 123;
  const int kBandwidthEstimateBytesPerSecond =
      kNumberOfPackets * kDefaultTCPMSS;
  cached_network_params.set_bandwidth_estimate_bytes_per_second(
      kBandwidthEstimateBytesPerSecond);
  cached_network_params.set_min_rtt_ms(1000);

  // Make sure that a bandwidth estimate results in a changed CWND.
  cached_network_params.set_timestamp(clock_.WallNow().ToUNIXSeconds() -
                                      (kNumSecondsPerHour - 1));
  sender_->ResumeConnectionState(cached_network_params, false);
  EXPECT_EQ(kNumberOfPackets * kDefaultTCPMSS, sender_->GetCongestionWindow());

  // Resumed CWND is limited to be in a sensible range.
  cached_network_params.set_bandwidth_estimate_bytes_per_second(
      (kMaxCongestionWindow + 1) * kDefaultTCPMSS);
  sender_->ResumeConnectionState(cached_network_params, false);
  EXPECT_EQ(kMaxCongestionWindow * kDefaultTCPMSS,
            sender_->GetCongestionWindow());

  if (FLAGS_quic_no_lower_bw_resumption_limit) {
    // Resume with an illegal value of 0 and verify the server uses 1 instead.
    cached_network_params.set_bandwidth_estimate_bytes_per_second(0);
    sender_->ResumeConnectionState(cached_network_params, false);
    EXPECT_EQ(sender_->min_congestion_window(), sender_->GetCongestionWindow());
  } else {
    cached_network_params.set_bandwidth_estimate_bytes_per_second(
        (kMinCongestionWindowForBandwidthResumption - 1) * kDefaultTCPMSS);
    sender_->ResumeConnectionState(cached_network_params, false);
    EXPECT_EQ(kMinCongestionWindowForBandwidthResumption * kDefaultTCPMSS,
              sender_->GetCongestionWindow());
  }

  // Resume to the max value.
  cached_network_params.set_max_bandwidth_estimate_bytes_per_second(
      kMaxCongestionWindow * kDefaultTCPMSS);
  sender_->ResumeConnectionState(cached_network_params, true);
  EXPECT_EQ(kMaxCongestionWindow * kDefaultTCPMSS,
            sender_->GetCongestionWindow());
}

TEST_F(TcpCubicSenderBytesTest, PaceBelowCWND) {
  QuicConfig config;

  // Verify that kCOPT: kMIN4 forces the min CWND to 1 packet, but allows up
  // to 4 to be sent.
  QuicTagVector options;
  options.push_back(kMIN4);
  QuicConfigPeer::SetReceivedConnectionOptions(&config, options);
  sender_->SetFromConfig(config, Perspective::IS_SERVER);
  sender_->OnRetransmissionTimeout(true);
  EXPECT_EQ(kDefaultTCPMSS, sender_->GetCongestionWindow());
  EXPECT_TRUE(
      sender_->TimeUntilSend(QuicTime::Zero(), kDefaultTCPMSS).IsZero());
  EXPECT_TRUE(
      sender_->TimeUntilSend(QuicTime::Zero(), 2 * kDefaultTCPMSS).IsZero());
  EXPECT_TRUE(
      sender_->TimeUntilSend(QuicTime::Zero(), 3 * kDefaultTCPMSS).IsZero());
  EXPECT_FALSE(
      sender_->TimeUntilSend(QuicTime::Zero(), 4 * kDefaultTCPMSS).IsZero());
}

TEST_F(TcpCubicSenderBytesTest, ResetAfterConnectionMigration) {
  // Starts from slow start.
  sender_->SetNumEmulatedConnections(1);
  const int kNumberOfAcks = 10;
  for (int i = 0; i < kNumberOfAcks; ++i) {
    // Send our full send window.
    SendAvailableSendWindow();
    AckNPackets(2);
  }
  SendAvailableSendWindow();
  QuicByteCount expected_send_window =
      kDefaultWindowTCP + (kDefaultTCPMSS * 2 * kNumberOfAcks);
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());

  // Loses a packet to exit slow start.
  LoseNPackets(1);

  // We should now have fallen out of slow start with a reduced window. Slow
  // start threshold is also updated.
  expected_send_window *= kRenoBeta;
  EXPECT_EQ(expected_send_window, sender_->GetCongestionWindow());
  EXPECT_EQ(expected_send_window, sender_->GetSlowStartThreshold());

  // Resets cwnd and slow start threshold on connection migrations.
  sender_->OnConnectionMigration();
  EXPECT_EQ(kDefaultWindowTCP, sender_->GetCongestionWindow());
  EXPECT_EQ(kMaxCongestionWindow * kDefaultTCPMSS,
            sender_->GetSlowStartThreshold());
  EXPECT_FALSE(sender_->hybrid_slow_start().started());
}

}  // namespace test
}  // namespace net
