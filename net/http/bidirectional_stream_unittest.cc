// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "net/http/bidirectional_stream.h"

#include <memory>

#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_piece.h"
#include "base/time/time.h"
#include "base/timer/mock_timer.h"
#include "net/base/net_errors.h"
#include "net/base/test_data_directory.h"
#include "net/http/bidirectional_stream_request_info.h"
#include "net/http/http_network_session.h"
#include "net/http/http_response_headers.h"
#include "net/http/http_server_properties.h"
#include "net/log/net_log.h"
#include "net/log/test_net_log.h"
#include "net/log/test_net_log_util.h"
#include "net/socket/socket_test_util.h"
#include "net/spdy/spdy_session.h"
#include "net/spdy/spdy_test_util_common.h"
#include "net/test/cert_test_util.h"
#include "net/url_request/url_request_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace net {

namespace {

const char kBodyData[] = "Body data";
const size_t kBodyDataSize = arraysize(kBodyData);
// Size of the buffer to be allocated for each read.
const size_t kReadBufferSize = 4096;

// Delegate that reads data but does not send any data.
class TestDelegateBase : public BidirectionalStream::Delegate {
 public:
  TestDelegateBase(IOBuffer* read_buf, int read_buf_len)
      : TestDelegateBase(read_buf,
                         read_buf_len,
                         base::WrapUnique(new base::Timer(false, false))) {}

  TestDelegateBase(IOBuffer* read_buf,
                   int read_buf_len,
                   std::unique_ptr<base::Timer> timer)
      : read_buf_(read_buf),
        read_buf_len_(read_buf_len),
        timer_(std::move(timer)),
        loop_(nullptr),
        error_(OK),
        on_data_read_count_(0),
        on_data_sent_count_(0),
        do_not_start_read_(false),
        run_until_completion_(false),
        not_expect_callback_(false),
        disable_auto_flush_(false) {}

  ~TestDelegateBase() override {}

  void OnStreamReady() override {
    if (callback_.is_null())
      return;
    callback_.Run(OK);
  }

  void OnHeadersReceived(const SpdyHeaderBlock& response_headers) override {
    CHECK(!not_expect_callback_);

    response_headers_ = response_headers;
    if (!do_not_start_read_)
      StartOrContinueReading();
  }

  void OnDataRead(int bytes_read) override {
    CHECK(!not_expect_callback_);

    ++on_data_read_count_;
    CHECK_GE(bytes_read, OK);
    data_received_.append(read_buf_->data(), bytes_read);
    if (!do_not_start_read_)
      StartOrContinueReading();
  }

  void OnDataSent() override {
    CHECK(!not_expect_callback_);

    ++on_data_sent_count_;
  }

  void OnTrailersReceived(const SpdyHeaderBlock& trailers) override {
    CHECK(!not_expect_callback_);

    trailers_ = trailers;
    if (run_until_completion_)
      loop_->Quit();
  }

  void OnFailed(int error) override {
    CHECK(!not_expect_callback_);
    CHECK_EQ(OK, error_);
    CHECK_NE(OK, error);

    error_ = error;
    if (run_until_completion_)
      loop_->Quit();
  }

  void DisableAutoFlush() { disable_auto_flush_ = true; }

  void Start(std::unique_ptr<BidirectionalStreamRequestInfo> request_info,
             HttpNetworkSession* session) {
    stream_.reset(new BidirectionalStream(std::move(request_info), session,
                                          false, this, std::move(timer_)));
    if (run_until_completion_)
      loop_->Run();
  }

  void Start(std::unique_ptr<BidirectionalStreamRequestInfo> request_info,
             HttpNetworkSession* session,
             const CompletionCallback& cb) {
    callback_ = cb;
    stream_.reset(new BidirectionalStream(std::move(request_info), session,
                                          disable_auto_flush_, this,
                                          std::move(timer_)));
    if (run_until_completion_)
      loop_->Run();
  }

  void SendData(IOBuffer* data, int length, bool end_of_stream) {
    not_expect_callback_ = true;
    stream_->SendData(data, length, end_of_stream);
    not_expect_callback_ = false;
  }

  void SendvData(const std::vector<IOBuffer*>& data,
                 const std::vector<int>& length,
                 bool end_of_stream) {
    not_expect_callback_ = true;
    stream_->SendvData(data, length, end_of_stream);
    not_expect_callback_ = false;
  }

  // Starts or continues reading data from |stream_| until no more bytes
  // can be read synchronously.
  void StartOrContinueReading() {
    int rv = ReadData();
    while (rv > 0) {
      rv = ReadData();
    }
    if (run_until_completion_ && rv == 0)
      loop_->Quit();
  }

  // Calls ReadData on the |stream_| and updates internal states.
  int ReadData() {
    not_expect_callback_ = true;
    int rv = stream_->ReadData(read_buf_.get(), read_buf_len_);
    not_expect_callback_ = false;
    if (rv > 0)
      data_received_.append(read_buf_->data(), rv);
    return rv;
  }

  // Cancels |stream_|.
  void CancelStream() { stream_->Cancel(); }

  // Deletes |stream_|.
  void DeleteStream() { stream_.reset(); }

  NextProto GetProtocol() const { return stream_->GetProtocol(); }

  int64_t GetTotalReceivedBytes() const {
    return stream_->GetTotalReceivedBytes();
  }

  int64_t GetTotalSentBytes() const { return stream_->GetTotalSentBytes(); }

  // Const getters for internal states.
  const std::string& data_received() const { return data_received_; }
  int error() const { return error_; }
  const SpdyHeaderBlock& response_headers() const { return response_headers_; }
  const SpdyHeaderBlock& trailers() const { return trailers_; }
  int on_data_read_count() const { return on_data_read_count_; }
  int on_data_sent_count() const { return on_data_sent_count_; }

  // Sets whether the delegate should automatically start reading.
  void set_do_not_start_read(bool do_not_start_read) {
    do_not_start_read_ = do_not_start_read;
  }
  // Sets whether the delegate should wait until the completion of the stream.
  void SetRunUntilCompletion(bool run_until_completion) {
    run_until_completion_ = run_until_completion;
    loop_.reset(new base::RunLoop);
  }

 protected:
  // Quits |loop_|.
  void QuitLoop() { loop_->Quit(); }

 private:
  std::unique_ptr<BidirectionalStream> stream_;
  scoped_refptr<IOBuffer> read_buf_;
  int read_buf_len_;
  std::unique_ptr<base::Timer> timer_;
  std::string data_received_;
  std::unique_ptr<base::RunLoop> loop_;
  SpdyHeaderBlock response_headers_;
  SpdyHeaderBlock trailers_;
  int error_;
  int on_data_read_count_;
  int on_data_sent_count_;
  bool do_not_start_read_;
  bool run_until_completion_;
  // This is to ensure that delegate callback is not invoked synchronously when
  // calling into |stream_|.
  bool not_expect_callback_;
  bool disable_auto_flush_;

  CompletionCallback callback_;
  DISALLOW_COPY_AND_ASSIGN(TestDelegateBase);
};

// A delegate that deletes the stream in a particular callback.
class CancelOrDeleteStreamDelegate : public TestDelegateBase {
 public:
  // Specifies in which callback the stream can be deleted.
  enum Phase {
    ON_HEADERS_RECEIVED,
    ON_DATA_READ,
    ON_TRAILERS_RECEIVED,
    ON_FAILED,
  };

  CancelOrDeleteStreamDelegate(IOBuffer* buf,
                               int buf_len,
                               Phase phase,
                               bool do_cancel)
      : TestDelegateBase(buf, buf_len), phase_(phase), do_cancel_(do_cancel) {}
  ~CancelOrDeleteStreamDelegate() override {}

  void OnHeadersReceived(const SpdyHeaderBlock& response_headers) override {
    TestDelegateBase::OnHeadersReceived(response_headers);
    if (phase_ == ON_HEADERS_RECEIVED) {
      CancelOrDelete();
      QuitLoop();
    }
  }

  void OnDataSent() override { NOTREACHED(); }

  void OnDataRead(int bytes_read) override {
    if (phase_ == ON_HEADERS_RECEIVED) {
      NOTREACHED();
      return;
    }
    TestDelegateBase::OnDataRead(bytes_read);
    if (phase_ == ON_DATA_READ) {
      CancelOrDelete();
      QuitLoop();
    }
  }

  void OnTrailersReceived(const SpdyHeaderBlock& trailers) override {
    if (phase_ == ON_HEADERS_RECEIVED || phase_ == ON_DATA_READ) {
      NOTREACHED();
      return;
    }
    TestDelegateBase::OnTrailersReceived(trailers);
    if (phase_ == ON_TRAILERS_RECEIVED) {
      CancelOrDelete();
      QuitLoop();
    }
  }

  void OnFailed(int error) override {
    if (phase_ != ON_FAILED) {
      NOTREACHED();
      return;
    }
    TestDelegateBase::OnFailed(error);
    CancelOrDelete();
    QuitLoop();
  }

 private:
  void CancelOrDelete() {
    if (do_cancel_) {
      CancelStream();
    } else {
      DeleteStream();
    }
  }

  // Indicates in which callback the delegate should cancel or delete the
  // stream.
  Phase phase_;
  // Indicates whether to cancel or delete the stream.
  bool do_cancel_;

  DISALLOW_COPY_AND_ASSIGN(CancelOrDeleteStreamDelegate);
};

// A Timer that does not start a delayed task unless the timer is fired.
class MockTimer : public base::MockTimer {
 public:
  MockTimer() : base::MockTimer(false, false) {}
  ~MockTimer() override {}

  void Start(const tracked_objects::Location& posted_from,
             base::TimeDelta delay,
             const base::Closure& user_task) override {
    // Sets a maximum delay, so the timer does not fire unless it is told to.
    base::TimeDelta infinite_delay = base::TimeDelta::Max();
    base::MockTimer::Start(posted_from, infinite_delay, user_task);
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(MockTimer);
};

}  // namespace

class BidirectionalStreamTest : public testing::TestWithParam<bool> {
 public:
  BidirectionalStreamTest()
      : spdy_util_(kProtoHTTP2, true),
        session_deps_(kProtoHTTP2),
        ssl_data_(SSLSocketDataProvider(ASYNC, OK)) {
    ssl_data_.SetNextProto(kProtoHTTP2);
    ssl_data_.cert = ImportCertFromFile(GetTestCertsDirectory(), "ok_cert.pem");
    net_log_.SetCaptureMode(NetLogCaptureMode::IncludeSocketBytes());
  }

 protected:
  void TearDown() override {
    if (sequenced_data_) {
      EXPECT_TRUE(sequenced_data_->AllReadDataConsumed());
      EXPECT_TRUE(sequenced_data_->AllWriteDataConsumed());
    }
  }

  // Initializes the session using SequencedSocketData.
  void InitSession(MockRead* reads,
                   size_t reads_count,
                   MockWrite* writes,
                   size_t writes_count,
                   const SpdySessionKey& key) {
    ASSERT_TRUE(ssl_data_.cert.get());
    session_deps_.socket_factory->AddSSLSocketDataProvider(&ssl_data_);
    sequenced_data_.reset(
        new SequencedSocketData(reads, reads_count, writes, writes_count));
    session_deps_.socket_factory->AddSocketDataProvider(sequenced_data_.get());
    session_deps_.net_log = net_log_.bound().net_log();
    http_session_ = SpdySessionDependencies::SpdyCreateSession(&session_deps_);
    session_ =
        CreateSecureSpdySession(http_session_.get(), key, net_log_.bound());
  }

  BoundTestNetLog net_log_;
  SpdyTestUtil spdy_util_;
  SpdySessionDependencies session_deps_;
  std::unique_ptr<SequencedSocketData> sequenced_data_;
  std::unique_ptr<HttpNetworkSession> http_session_;

 private:
  SSLSocketDataProvider ssl_data_;
  base::WeakPtr<SpdySession> session_;
};

TEST_F(BidirectionalStreamTest, CreateInsecureStream) {
  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "GET";
  request_info->url = GURL("http://www.example.org/");

  TestDelegateBase delegate(nullptr, 0);
  HttpNetworkSession::Params params =
      SpdySessionDependencies::CreateSessionParams(&session_deps_);
  std::unique_ptr<HttpNetworkSession> session(new HttpNetworkSession(params));
  delegate.SetRunUntilCompletion(true);
  delegate.Start(std::move(request_info), session.get());

  EXPECT_EQ(ERR_DISALLOWED_URL_SCHEME, delegate.error());
}

// Simulates user calling ReadData after END_STREAM has been received in
// BidirectionalStreamSpdyImpl.
TEST_F(BidirectionalStreamTest, TestReadDataAfterClose) {
  std::unique_ptr<SpdySerializedFrame> req(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));
  // Empty DATA frame with an END_STREAM flag.
  std::unique_ptr<SpdySerializedFrame> end_stream(
      spdy_util_.ConstructSpdyBodyFrame(1, nullptr, 0, true));
  MockWrite writes[] = {
      CreateMockWrite(*req.get(), 0),
  };

  const char* const kExtraResponseHeaders[] = {"header-name", "header-value"};

  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(kExtraResponseHeaders, 1, 1));

  std::unique_ptr<SpdySerializedFrame> body_frame(
      spdy_util_.ConstructSpdyBodyFrame(1, false));
  // Last body frame has END_STREAM flag set.
  std::unique_ptr<SpdySerializedFrame> last_body_frame(
      spdy_util_.ConstructSpdyBodyFrame(1, true));

  MockRead reads[] = {
      CreateMockRead(*resp, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 2),  // Force a pause.
      CreateMockRead(*body_frame, 3),
      MockRead(ASYNC, ERR_IO_PENDING, 4),  // Force a pause.
      CreateMockRead(*body_frame, 5),
      CreateMockRead(*last_body_frame, 6),
      MockRead(SYNCHRONOUS, 0, 7),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "GET";
  request_info->url = GURL("https://www.example.org/");
  request_info->end_stream_on_headers = true;
  request_info->priority = LOWEST;

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  // Create a MockTimer. Retain a raw pointer since the underlying
  // BidirectionalStreamImpl owns it.
  MockTimer* timer = new MockTimer();
  std::unique_ptr<TestDelegateBase> delegate(new TestDelegateBase(
      read_buffer.get(), kReadBufferSize, base::WrapUnique(timer)));
  delegate->set_do_not_start_read(true);

  delegate->Start(std::move(request_info), http_session_.get());

  // Write request, and deliver response headers.
  sequenced_data_->RunUntilPaused();
  EXPECT_FALSE(timer->IsRunning());
  // ReadData returns asynchronously because no data is buffered.
  int rv = delegate->ReadData();
  EXPECT_EQ(ERR_IO_PENDING, rv);
  // Deliver a DATA frame.
  sequenced_data_->Resume();
  base::RunLoop().RunUntilIdle();
  timer->Fire();
  // Asynchronous completion callback is invoke.
  EXPECT_EQ(1, delegate->on_data_read_count());
  EXPECT_EQ(kUploadDataSize * 1,
            static_cast<int>(delegate->data_received().size()));

  // Deliver the rest. Note that user has not called a second ReadData.
  sequenced_data_->Resume();
  base::RunLoop().RunUntilIdle();
  // ReadData now. Read should complete synchronously.
  rv = delegate->ReadData();
  EXPECT_EQ(kUploadDataSize * 2, rv);
  rv = delegate->ReadData();
  EXPECT_EQ(OK, rv);  // EOF.

  const SpdyHeaderBlock response_headers = delegate->response_headers();
  EXPECT_EQ("200", response_headers.find(":status")->second);
  EXPECT_EQ("header-value", response_headers.find("header-name")->second);
  EXPECT_EQ(1, delegate->on_data_read_count());
  EXPECT_EQ(0, delegate->on_data_sent_count());
  EXPECT_EQ(kProtoHTTP2, delegate->GetProtocol());
  EXPECT_EQ(CountWriteBytes(writes, arraysize(writes)),
            delegate->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads, arraysize(reads)),
            delegate->GetTotalReceivedBytes());
}

// Tests that the NetLog contains correct entries.
TEST_F(BidirectionalStreamTest, TestNetLogContainEntries) {
  BufferedSpdyFramer framer(spdy_util_.spdy_version());

  std::unique_ptr<SpdySerializedFrame> req(spdy_util_.ConstructSpdyPost(
      "https://www.example.org", 1, kBodyDataSize * 3, LOWEST, nullptr, 0));
  std::unique_ptr<SpdySerializedFrame> data_frame(
      framer.CreateDataFrame(1, kBodyData, kBodyDataSize, DATA_FLAG_FIN));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*data_frame, 3),
  };

  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  std::unique_ptr<SpdySerializedFrame> response_body_frame1(
      spdy_util_.ConstructSpdyBodyFrame(1, false));
  std::unique_ptr<SpdySerializedFrame> response_body_frame2(
      spdy_util_.ConstructSpdyBodyFrame(1, false));

  SpdyHeaderBlock trailers;
  trailers["foo"] = "bar";
  std::unique_ptr<SpdySerializedFrame> response_trailers(
      spdy_util_.ConstructSpdyResponseHeaders(1, trailers, true));

  MockRead reads[] = {
      CreateMockRead(*resp, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 2),  // Force a pause.
      CreateMockRead(*response_body_frame1, 4),
      MockRead(ASYNC, ERR_IO_PENDING, 5),  // Force a pause.
      CreateMockRead(*response_body_frame2, 6),
      CreateMockRead(*response_trailers, 7),
      MockRead(ASYNC, 0, 8),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "POST";
  request_info->url = GURL("https://www.example.org/");
  request_info->priority = LOWEST;
  request_info->extra_headers.SetHeader(net::HttpRequestHeaders::kContentLength,
                                        base::SizeTToString(kBodyDataSize * 3));

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  MockTimer* timer = new MockTimer();
  std::unique_ptr<TestDelegateBase> delegate(new TestDelegateBase(
      read_buffer.get(), kReadBufferSize, base::WrapUnique(timer)));
  delegate->set_do_not_start_read(true);
  delegate->Start(std::move(request_info), http_session_.get());
  // Send the request and receive response headers.
  sequenced_data_->RunUntilPaused();
  EXPECT_FALSE(timer->IsRunning());

  scoped_refptr<StringIOBuffer> buf(
      new StringIOBuffer(std::string(kBodyData, kBodyDataSize)));
  // Send a DATA frame.
  delegate->SendData(buf.get(), buf->size(), true);
  // ReadData returns asynchronously because no data is buffered.
  int rv = delegate->ReadData();
  EXPECT_EQ(ERR_IO_PENDING, rv);
  // Deliver the first DATA frame.
  sequenced_data_->Resume();
  sequenced_data_->RunUntilPaused();
  // |sequenced_data_| is now stopped after delivering first DATA frame but
  // before the second DATA frame.
  // Fire the timer to allow the first ReadData to complete asynchronously.
  timer->Fire();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, delegate->on_data_read_count());

  // Now let |sequenced_data_| run until completion.
  sequenced_data_->Resume();
  base::RunLoop().RunUntilIdle();
  // All data has been delivered, and OnClosed() has been invoked.
  // Read now, and it should complete synchronously.
  rv = delegate->ReadData();
  EXPECT_EQ(kUploadDataSize, rv);
  EXPECT_EQ("200", delegate->response_headers().find(":status")->second);
  EXPECT_EQ(1, delegate->on_data_read_count());
  EXPECT_EQ(1, delegate->on_data_sent_count());
  EXPECT_EQ(kProtoHTTP2, delegate->GetProtocol());
  EXPECT_EQ("bar", delegate->trailers().find("foo")->second);
  EXPECT_EQ(CountWriteBytes(writes, arraysize(writes)),
            delegate->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads, arraysize(reads)),
            delegate->GetTotalReceivedBytes());

  // Destroy the delegate will destroy the stream, so we can get an end event
  // for BIDIRECTIONAL_STREAM_ALIVE.
  delegate.reset();
  TestNetLogEntry::List entries;
  net_log_.GetEntries(&entries);
  size_t index = ExpectLogContainsSomewhere(
      entries, 0, NetLog::TYPE_BIDIRECTIONAL_STREAM_ALIVE, NetLog::PHASE_BEGIN);
  // Headers received should happen after BIDIRECTIONAL_STREAM_ALIVE.
  index = ExpectLogContainsSomewhere(
      entries, index, NetLog::TYPE_BIDIRECTIONAL_STREAM_RECV_HEADERS,
      NetLog::PHASE_NONE);
  // Trailers received should happen after headers received. It might happen
  // before the reads complete.
  ExpectLogContainsSomewhere(entries, index,
                             NetLog::TYPE_BIDIRECTIONAL_STREAM_RECV_TRAILERS,
                             NetLog::PHASE_NONE);
  // Sent bytes. Sending data is always asynchronous.
  index = ExpectLogContainsSomewhere(
      entries, index, NetLog::TYPE_BIDIRECTIONAL_STREAM_BYTES_SENT,
      NetLog::PHASE_NONE);
  TestNetLogEntry entry = entries[index];
  EXPECT_EQ(NetLog::SOURCE_BIDIRECTIONAL_STREAM, entry.source.type);
  // Received bytes for asynchronous read.
  index = ExpectLogContainsSomewhere(
      entries, index, NetLog::TYPE_BIDIRECTIONAL_STREAM_BYTES_RECEIVED,
      NetLog::PHASE_NONE);
  entry = entries[index];
  EXPECT_EQ(NetLog::SOURCE_BIDIRECTIONAL_STREAM, entry.source.type);
  // Received bytes for synchronous read.
  index = ExpectLogContainsSomewhere(
      entries, index, NetLog::TYPE_BIDIRECTIONAL_STREAM_BYTES_RECEIVED,
      NetLog::PHASE_NONE);
  entry = entries[index];
  EXPECT_EQ(NetLog::SOURCE_BIDIRECTIONAL_STREAM, entry.source.type);
  ExpectLogContainsSomewhere(entries, index,
                             NetLog::TYPE_BIDIRECTIONAL_STREAM_ALIVE,
                             NetLog::PHASE_END);
}

TEST_F(BidirectionalStreamTest, TestInterleaveReadDataAndSendData) {
  BufferedSpdyFramer framer(spdy_util_.spdy_version());

  std::unique_ptr<SpdySerializedFrame> req(spdy_util_.ConstructSpdyPost(
      "https://www.example.org", 1, kBodyDataSize * 3, LOWEST, nullptr, 0));
  std::unique_ptr<SpdySerializedFrame> data_frame1(
      framer.CreateDataFrame(1, kBodyData, kBodyDataSize, DATA_FLAG_NONE));
  std::unique_ptr<SpdySerializedFrame> data_frame2(
      framer.CreateDataFrame(1, kBodyData, kBodyDataSize, DATA_FLAG_NONE));
  std::unique_ptr<SpdySerializedFrame> data_frame3(
      framer.CreateDataFrame(1, kBodyData, kBodyDataSize, DATA_FLAG_FIN));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*data_frame1, 3),
      CreateMockWrite(*data_frame2, 6), CreateMockWrite(*data_frame3, 9),
  };

  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  std::unique_ptr<SpdySerializedFrame> response_body_frame1(
      spdy_util_.ConstructSpdyBodyFrame(1, false));
  std::unique_ptr<SpdySerializedFrame> response_body_frame2(
      spdy_util_.ConstructSpdyBodyFrame(1, true));

  MockRead reads[] = {
      CreateMockRead(*resp, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 2),  // Force a pause.
      CreateMockRead(*response_body_frame1, 4),
      MockRead(ASYNC, ERR_IO_PENDING, 5),  // Force a pause.
      CreateMockRead(*response_body_frame2, 7),
      MockRead(ASYNC, ERR_IO_PENDING, 8),  // Force a pause.
      MockRead(ASYNC, 0, 10),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "POST";
  request_info->url = GURL("https://www.example.org/");
  request_info->priority = LOWEST;
  request_info->extra_headers.SetHeader(net::HttpRequestHeaders::kContentLength,
                                        base::SizeTToString(kBodyDataSize * 3));

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  MockTimer* timer = new MockTimer();
  std::unique_ptr<TestDelegateBase> delegate(new TestDelegateBase(
      read_buffer.get(), kReadBufferSize, base::WrapUnique(timer)));
  delegate->set_do_not_start_read(true);
  delegate->Start(std::move(request_info), http_session_.get());
  // Send the request and receive response headers.
  sequenced_data_->RunUntilPaused();
  EXPECT_FALSE(timer->IsRunning());

  // Send a DATA frame.
  scoped_refptr<StringIOBuffer> buf(
      new StringIOBuffer(std::string(kBodyData, kBodyDataSize)));

  // Send a DATA frame.
  delegate->SendData(buf.get(), buf->size(), false);
  // ReadData and it should return asynchronously because no data is buffered.
  int rv = delegate->ReadData();
  EXPECT_EQ(ERR_IO_PENDING, rv);
  // Deliver a DATA frame, and fire the timer.
  sequenced_data_->Resume();
  sequenced_data_->RunUntilPaused();
  timer->Fire();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, delegate->on_data_sent_count());
  EXPECT_EQ(1, delegate->on_data_read_count());

  // Send a DATA frame.
  delegate->SendData(buf.get(), buf->size(), false);
  // ReadData and it should return asynchronously because no data is buffered.
  rv = delegate->ReadData();
  EXPECT_EQ(ERR_IO_PENDING, rv);
  // Deliver a DATA frame, and fire the timer.
  sequenced_data_->Resume();
  sequenced_data_->RunUntilPaused();
  timer->Fire();
  base::RunLoop().RunUntilIdle();
  // Last DATA frame is read. Server half closes.
  EXPECT_EQ(2, delegate->on_data_read_count());
  EXPECT_EQ(2, delegate->on_data_sent_count());

  // Send the last body frame. Client half closes.
  delegate->SendData(buf.get(), buf->size(), true);
  sequenced_data_->Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(3, delegate->on_data_sent_count());

  // OnClose is invoked since both sides are closed.
  rv = delegate->ReadData();
  EXPECT_EQ(OK, rv);

  EXPECT_EQ("200", delegate->response_headers().find(":status")->second);
  EXPECT_EQ(2, delegate->on_data_read_count());
  EXPECT_EQ(3, delegate->on_data_sent_count());
  EXPECT_EQ(kProtoHTTP2, delegate->GetProtocol());
  EXPECT_EQ(CountWriteBytes(writes, arraysize(writes)),
            delegate->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads, arraysize(reads)),
            delegate->GetTotalReceivedBytes());
}

TEST_F(BidirectionalStreamTest, TestCoalesceSmallDataBuffers) {
  BufferedSpdyFramer framer(spdy_util_.spdy_version());

  std::unique_ptr<SpdySerializedFrame> req(spdy_util_.ConstructSpdyPost(
      "https://www.example.org", 1, kBodyDataSize * 1, LOWEST, nullptr, 0));
  std::string body_data = "some really long piece of data";
  std::unique_ptr<SpdySerializedFrame> data_frame1(framer.CreateDataFrame(
      1, body_data.c_str(), body_data.size(), DATA_FLAG_FIN));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*data_frame1, 1),
  };

  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  std::unique_ptr<SpdySerializedFrame> response_body_frame1(
      spdy_util_.ConstructSpdyBodyFrame(1, true));
  MockRead reads[] = {
      CreateMockRead(*resp, 2),
      MockRead(ASYNC, ERR_IO_PENDING, 3),  // Force a pause.
      CreateMockRead(*response_body_frame1, 4), MockRead(ASYNC, 0, 5),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "POST";
  request_info->url = GURL("https://www.example.org/");
  request_info->priority = LOWEST;
  request_info->extra_headers.SetHeader(net::HttpRequestHeaders::kContentLength,
                                        base::SizeTToString(kBodyDataSize * 1));

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  MockTimer* timer = new MockTimer();
  std::unique_ptr<TestDelegateBase> delegate(new TestDelegateBase(
      read_buffer.get(), kReadBufferSize, base::WrapUnique(timer)));
  delegate->set_do_not_start_read(true);
  delegate->DisableAutoFlush();
  TestCompletionCallback callback;
  delegate->Start(std::move(request_info), http_session_.get(),
                  callback.callback());
  // Wait until the stream is ready.
  callback.WaitForResult();
  // Send a DATA frame.
  scoped_refptr<StringIOBuffer> buf(new StringIOBuffer(body_data.substr(0, 5)));
  scoped_refptr<StringIOBuffer> buf2(
      new StringIOBuffer(body_data.substr(5, body_data.size() - 5)));
  delegate->SendvData({buf.get(), buf2.get()}, {buf->size(), buf2->size()},
                      true);
  sequenced_data_->RunUntilPaused();  // OnHeadersReceived.
  // ReadData and it should return asynchronously because no data is buffered.
  EXPECT_EQ(ERR_IO_PENDING, delegate->ReadData());
  sequenced_data_->Resume();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, delegate->on_data_sent_count());
  EXPECT_EQ(1, delegate->on_data_read_count());

  EXPECT_EQ("200", delegate->response_headers().find(":status")->second);
  EXPECT_EQ(1, delegate->on_data_read_count());
  EXPECT_EQ(1, delegate->on_data_sent_count());
  EXPECT_EQ(kProtoHTTP2, delegate->GetProtocol());
  EXPECT_EQ(CountWriteBytes(writes, arraysize(writes)),
            delegate->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads, arraysize(reads)),
            delegate->GetTotalReceivedBytes());
}

// Tests that BidirectionalStreamSpdyImpl::OnClose will complete any remaining
// read even if the read queue is empty.
TEST_F(BidirectionalStreamTest, TestCompleteAsyncRead) {
  std::unique_ptr<SpdySerializedFrame> req(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));
  // Empty DATA frame with an END_STREAM flag.
  std::unique_ptr<SpdySerializedFrame> end_stream(
      spdy_util_.ConstructSpdyBodyFrame(1, nullptr, 0, true));

  MockWrite writes[] = {CreateMockWrite(*req.get(), 0)};

  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));

  std::unique_ptr<SpdySerializedFrame> response_body_frame(
      spdy_util_.ConstructSpdyBodyFrame(1, nullptr, 0, true));

  MockRead reads[] = {
      CreateMockRead(*resp, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 2),  // Force a pause.
      CreateMockRead(*response_body_frame, 3), MockRead(SYNCHRONOUS, 0, 4),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "GET";
  request_info->url = GURL("https://www.example.org/");
  request_info->priority = LOWEST;
  request_info->end_stream_on_headers = true;

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  MockTimer* timer = new MockTimer();
  std::unique_ptr<TestDelegateBase> delegate(new TestDelegateBase(
      read_buffer.get(), kReadBufferSize, base::WrapUnique(timer)));
  delegate->set_do_not_start_read(true);
  delegate->Start(std::move(request_info), http_session_.get());
  // Write request, and deliver response headers.
  sequenced_data_->RunUntilPaused();
  EXPECT_FALSE(timer->IsRunning());

  // ReadData should return asynchronously because no data is buffered.
  int rv = delegate->ReadData();
  EXPECT_EQ(ERR_IO_PENDING, rv);
  // Deliver END_STREAM.
  // OnClose should trigger completion of the remaining read.
  sequenced_data_->Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ("200", delegate->response_headers().find(":status")->second);
  EXPECT_EQ(1, delegate->on_data_read_count());
  EXPECT_EQ(0u, delegate->data_received().size());
  EXPECT_EQ(0, delegate->on_data_sent_count());
  EXPECT_EQ(kProtoHTTP2, delegate->GetProtocol());
  EXPECT_EQ(CountWriteBytes(writes, arraysize(writes)),
            delegate->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads, arraysize(reads)),
            delegate->GetTotalReceivedBytes());
}

TEST_F(BidirectionalStreamTest, TestBuffering) {
  std::unique_ptr<SpdySerializedFrame> req(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));
  // Empty DATA frame with an END_STREAM flag.
  std::unique_ptr<SpdySerializedFrame> end_stream(
      spdy_util_.ConstructSpdyBodyFrame(1, nullptr, 0, true));

  MockWrite writes[] = {CreateMockWrite(*req.get(), 0)};

  const char* const kExtraResponseHeaders[] = {"header-name", "header-value"};

  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(kExtraResponseHeaders, 1, 1));

  std::unique_ptr<SpdySerializedFrame> body_frame(
      spdy_util_.ConstructSpdyBodyFrame(1, false));
  // Last body frame has END_STREAM flag set.
  std::unique_ptr<SpdySerializedFrame> last_body_frame(
      spdy_util_.ConstructSpdyBodyFrame(1, true));

  MockRead reads[] = {
      CreateMockRead(*resp, 1),
      CreateMockRead(*body_frame, 2),
      CreateMockRead(*body_frame, 3),
      MockRead(ASYNC, ERR_IO_PENDING, 4),  // Force a pause.
      CreateMockRead(*last_body_frame, 5),
      MockRead(SYNCHRONOUS, 0, 6),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "GET";
  request_info->url = GURL("https://www.example.org/");
  request_info->priority = LOWEST;
  request_info->end_stream_on_headers = true;

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  MockTimer* timer = new MockTimer();
  std::unique_ptr<TestDelegateBase> delegate(new TestDelegateBase(
      read_buffer.get(), kReadBufferSize, base::WrapUnique(timer)));
  delegate->Start(std::move(request_info), http_session_.get());
  // Deliver two DATA frames together.
  sequenced_data_->RunUntilPaused();
  EXPECT_TRUE(timer->IsRunning());
  timer->Fire();
  base::RunLoop().RunUntilIdle();
  // This should trigger |more_read_data_pending_| to execute the task at a
  // later time, and Delegate::OnReadComplete should not have been called.
  EXPECT_TRUE(timer->IsRunning());
  EXPECT_EQ(0, delegate->on_data_read_count());

  // Fire the timer now, the two DATA frame should be combined into one
  // single Delegate::OnReadComplete callback.
  timer->Fire();
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(1, delegate->on_data_read_count());
  EXPECT_EQ(kUploadDataSize * 2,
            static_cast<int>(delegate->data_received().size()));

  // Deliver last DATA frame and EOF. There will be an additional
  // Delegate::OnReadComplete callback.
  sequenced_data_->Resume();
  EXPECT_EQ(2, delegate->on_data_read_count());
  EXPECT_EQ(kUploadDataSize * 3,
            static_cast<int>(delegate->data_received().size()));

  const SpdyHeaderBlock response_headers = delegate->response_headers();
  EXPECT_EQ("200", response_headers.find(":status")->second);
  EXPECT_EQ("header-value", response_headers.find("header-name")->second);
  EXPECT_EQ(0, delegate->on_data_sent_count());
  EXPECT_EQ(kProtoHTTP2, delegate->GetProtocol());
  EXPECT_EQ(CountWriteBytes(writes, arraysize(writes)),
            delegate->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads, arraysize(reads)),
            delegate->GetTotalReceivedBytes());
}

TEST_F(BidirectionalStreamTest, TestBufferingWithTrailers) {
  std::unique_ptr<SpdySerializedFrame> req(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));
  // Empty DATA frame with an END_STREAM flag.
  std::unique_ptr<SpdySerializedFrame> end_stream(
      spdy_util_.ConstructSpdyBodyFrame(1, nullptr, 0, true));

  MockWrite writes[] = {
      CreateMockWrite(*req.get(), 0),
  };

  const char* const kExtraResponseHeaders[] = {"header-name", "header-value"};

  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(kExtraResponseHeaders, 1, 1));

  std::unique_ptr<SpdySerializedFrame> body_frame(
      spdy_util_.ConstructSpdyBodyFrame(1, false));

  SpdyHeaderBlock trailers;
  trailers["foo"] = "bar";
  std::unique_ptr<SpdySerializedFrame> response_trailers(
      spdy_util_.ConstructSpdyResponseHeaders(1, trailers, true));

  MockRead reads[] = {
      CreateMockRead(*resp, 1),
      CreateMockRead(*body_frame, 2),
      CreateMockRead(*body_frame, 3),
      CreateMockRead(*body_frame, 4),
      MockRead(ASYNC, ERR_IO_PENDING, 5),  // Force a pause.
      CreateMockRead(*response_trailers, 6),
      MockRead(SYNCHRONOUS, 0, 7),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  MockTimer* timer = new MockTimer();
  std::unique_ptr<TestDelegateBase> delegate(new TestDelegateBase(
      read_buffer.get(), kReadBufferSize, base::WrapUnique(timer)));

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "GET";
  request_info->url = GURL("https://www.example.org/");
  request_info->priority = LOWEST;
  request_info->end_stream_on_headers = true;

  delegate->Start(std::move(request_info), http_session_.get());
  // Deliver all three DATA frames together.
  sequenced_data_->RunUntilPaused();
  EXPECT_TRUE(timer->IsRunning());
  timer->Fire();
  base::RunLoop().RunUntilIdle();
  // This should trigger |more_read_data_pending_| to execute the task at a
  // later time, and Delegate::OnReadComplete should not have been called.
  EXPECT_TRUE(timer->IsRunning());
  EXPECT_EQ(0, delegate->on_data_read_count());

  // Deliver trailers. Remaining read should be completed, since OnClose is
  // called right after OnTrailersReceived. The three DATA frames should be
  // delivered in a single OnReadCompleted callback.
  sequenced_data_->Resume();
  EXPECT_EQ(1, delegate->on_data_read_count());
  EXPECT_EQ(kUploadDataSize * 3,
            static_cast<int>(delegate->data_received().size()));
  const SpdyHeaderBlock response_headers = delegate->response_headers();
  EXPECT_EQ("200", response_headers.find(":status")->second);
  EXPECT_EQ("header-value", response_headers.find("header-name")->second);
  EXPECT_EQ("bar", delegate->trailers().find("foo")->second);
  EXPECT_EQ(0, delegate->on_data_sent_count());
  EXPECT_EQ(kProtoHTTP2, delegate->GetProtocol());
  EXPECT_EQ(CountWriteBytes(writes, arraysize(writes)),
            delegate->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads, arraysize(reads)),
            delegate->GetTotalReceivedBytes());
}

TEST_F(BidirectionalStreamTest, CancelStreamAfterSendData) {
  BufferedSpdyFramer framer(spdy_util_.spdy_version());

  std::unique_ptr<SpdySerializedFrame> req(spdy_util_.ConstructSpdyPost(
      "https://www.example.org", 1, kBodyDataSize * 3, LOWEST, nullptr, 0));
  std::unique_ptr<SpdySerializedFrame> data_frame(
      framer.CreateDataFrame(1, kBodyData, kBodyDataSize, DATA_FLAG_NONE));
  std::unique_ptr<SpdySerializedFrame> rst(
      spdy_util_.ConstructSpdyRstStream(1, RST_STREAM_CANCEL));

  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*data_frame, 3),
      CreateMockWrite(*rst, 5),
  };

  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  std::unique_ptr<SpdySerializedFrame> response_body_frame(
      spdy_util_.ConstructSpdyBodyFrame(1, false));

  MockRead reads[] = {
      CreateMockRead(*resp, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 2),  // Force a pause.
      MockRead(ASYNC, ERR_IO_PENDING, 4),  // Force a pause.
      MockRead(ASYNC, 0, 6),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "POST";
  request_info->url = GURL("https://www.example.org/");
  request_info->priority = LOWEST;
  request_info->extra_headers.SetHeader(net::HttpRequestHeaders::kContentLength,
                                        base::SizeTToString(kBodyDataSize * 3));

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  std::unique_ptr<TestDelegateBase> delegate(
      new TestDelegateBase(read_buffer.get(), kReadBufferSize));
  delegate->set_do_not_start_read(true);
  delegate->Start(std::move(request_info), http_session_.get());
  // Send the request and receive response headers.
  sequenced_data_->RunUntilPaused();
  EXPECT_EQ(kProtoHTTP2, delegate->GetProtocol());

  // Send a DATA frame.
  scoped_refptr<StringIOBuffer> buf(
      new StringIOBuffer(std::string(kBodyData, kBodyDataSize)));
  delegate->SendData(buf.get(), buf->size(), false);
  sequenced_data_->Resume();
  base::RunLoop().RunUntilIdle();
  // Cancel the stream.
  delegate->CancelStream();
  sequenced_data_->Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ("200", delegate->response_headers().find(":status")->second);
  EXPECT_EQ(0, delegate->on_data_read_count());
  // EXPECT_EQ(1, delegate->on_data_send_count());
  // OnDataSent may or may not have been invoked.
  // Calling after stream is canceled gives kProtoUnknown.
  EXPECT_EQ(kProtoUnknown, delegate->GetProtocol());
  EXPECT_EQ(0, delegate->GetTotalSentBytes());
  EXPECT_EQ(0, delegate->GetTotalReceivedBytes());
}

TEST_F(BidirectionalStreamTest, CancelStreamDuringReadData) {
  BufferedSpdyFramer framer(spdy_util_.spdy_version());

  std::unique_ptr<SpdySerializedFrame> req(spdy_util_.ConstructSpdyPost(
      "https://www.example.org", 1, kBodyDataSize * 3, LOWEST, nullptr, 0));
  std::unique_ptr<SpdySerializedFrame> data_frame(
      framer.CreateDataFrame(1, kBodyData, kBodyDataSize, DATA_FLAG_NONE));
  std::unique_ptr<SpdySerializedFrame> rst(
      spdy_util_.ConstructSpdyRstStream(1, RST_STREAM_CANCEL));

  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*rst, 4),
  };

  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(nullptr, 0, 1));
  std::unique_ptr<SpdySerializedFrame> response_body_frame(
      spdy_util_.ConstructSpdyBodyFrame(1, false));

  MockRead reads[] = {
      CreateMockRead(*resp, 1),
      MockRead(ASYNC, ERR_IO_PENDING, 2),  // Force a pause.
      CreateMockRead(*response_body_frame, 3), MockRead(ASYNC, 0, 5),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "POST";
  request_info->url = GURL("https://www.example.org/");
  request_info->priority = LOWEST;
  request_info->extra_headers.SetHeader(net::HttpRequestHeaders::kContentLength,
                                        base::SizeTToString(kBodyDataSize * 3));

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  std::unique_ptr<TestDelegateBase> delegate(
      new TestDelegateBase(read_buffer.get(), kReadBufferSize));
  delegate->set_do_not_start_read(true);
  delegate->Start(std::move(request_info), http_session_.get());
  // Send the request and receive response headers.
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ("200", delegate->response_headers().find(":status")->second);
  // Cancel the stream after ReadData returns ERR_IO_PENDING.
  int rv = delegate->ReadData();
  EXPECT_EQ(kProtoHTTP2, delegate->GetProtocol());
  EXPECT_EQ(ERR_IO_PENDING, rv);
  delegate->CancelStream();
  sequenced_data_->Resume();
  base::RunLoop().RunUntilIdle();

  EXPECT_EQ(0, delegate->on_data_read_count());
  EXPECT_EQ(0, delegate->on_data_sent_count());
  // Calling after stream is canceled gives kProtoUnknown.
  EXPECT_EQ(kProtoUnknown, delegate->GetProtocol());
  EXPECT_EQ(0, delegate->GetTotalSentBytes());
  EXPECT_EQ(0, delegate->GetTotalReceivedBytes());
}

// Receiving a header with uppercase ASCII will result in a protocol error,
// which should be propagated via Delegate::OnFailed.
TEST_F(BidirectionalStreamTest, PropagateProtocolError) {
  std::unique_ptr<SpdySerializedFrame> req(spdy_util_.ConstructSpdyPost(
      "https://www.example.org", 1, kBodyDataSize * 3, LOW, nullptr, 0));
  std::unique_ptr<SpdySerializedFrame> rst(
      spdy_util_.ConstructSpdyRstStream(1, RST_STREAM_PROTOCOL_ERROR));

  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*rst, 2),
  };

  const char* const kExtraHeaders[] = {"X-UpperCase", "yes"};
  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(kExtraHeaders, 1, 1));

  MockRead reads[] = {
      CreateMockRead(*resp, 1), MockRead(ASYNC, 0, 3),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "POST";
  request_info->url = GURL("https://www.example.org/");
  request_info->extra_headers.SetHeader(net::HttpRequestHeaders::kContentLength,
                                        base::SizeTToString(kBodyDataSize * 3));

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  std::unique_ptr<TestDelegateBase> delegate(
      new TestDelegateBase(read_buffer.get(), kReadBufferSize));
  delegate->SetRunUntilCompletion(true);
  delegate->Start(std::move(request_info), http_session_.get());

  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(ERR_SPDY_PROTOCOL_ERROR, delegate->error());
  EXPECT_EQ(delegate->response_headers().end(),
            delegate->response_headers().find(":status"));
  EXPECT_EQ(0, delegate->on_data_read_count());
  EXPECT_EQ(0, delegate->on_data_sent_count());
  EXPECT_EQ(kProtoHTTP2, delegate->GetProtocol());
  // BidirectionalStreamSpdyStreamJob does not count the bytes sent for |rst|
  // because it is sent after SpdyStream::Delegate::OnClose is called.
  EXPECT_EQ(CountWriteBytes(writes, 1), delegate->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads, arraysize(reads)),
            delegate->GetTotalReceivedBytes());
}

INSTANTIATE_TEST_CASE_P(CancelOrDeleteTests,
                        BidirectionalStreamTest,
                        ::testing::Values(true, false));

TEST_P(BidirectionalStreamTest, CancelOrDeleteStreamDuringOnHeadersReceived) {
  std::unique_ptr<SpdySerializedFrame> req(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));

  std::unique_ptr<SpdySerializedFrame> rst(
      spdy_util_.ConstructSpdyRstStream(1, RST_STREAM_CANCEL));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*rst, 2),
  };

  const char* const kExtraResponseHeaders[] = {"header-name", "header-value"};

  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(kExtraResponseHeaders, 1, 1));

  MockRead reads[] = {
      CreateMockRead(*resp, 1), MockRead(ASYNC, 0, 3),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "GET";
  request_info->url = GURL("https://www.example.org/");
  request_info->priority = LOWEST;
  request_info->end_stream_on_headers = true;

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  std::unique_ptr<CancelOrDeleteStreamDelegate> delegate(
      new CancelOrDeleteStreamDelegate(
          read_buffer.get(), kReadBufferSize,
          CancelOrDeleteStreamDelegate::Phase::ON_HEADERS_RECEIVED,
          GetParam()));
  delegate->SetRunUntilCompletion(true);
  delegate->Start(std::move(request_info), http_session_.get());
  // Makes sure delegate does not get called.
  base::RunLoop().RunUntilIdle();
  const SpdyHeaderBlock response_headers = delegate->response_headers();
  EXPECT_EQ("200", response_headers.find(":status")->second);
  EXPECT_EQ("header-value", response_headers.find("header-name")->second);
  EXPECT_EQ(0u, delegate->data_received().size());
  EXPECT_EQ(0, delegate->on_data_sent_count());
  EXPECT_EQ(0, delegate->on_data_read_count());

  // If stream is destroyed, do not call into stream.
  if (!GetParam())
    return;
  EXPECT_EQ(0, delegate->GetTotalSentBytes());
  EXPECT_EQ(0, delegate->GetTotalReceivedBytes());
  EXPECT_EQ(kProtoUnknown, delegate->GetProtocol());
}

TEST_P(BidirectionalStreamTest, CancelOrDeleteStreamDuringOnDataRead) {
  std::unique_ptr<SpdySerializedFrame> req(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));

  std::unique_ptr<SpdySerializedFrame> rst(
      spdy_util_.ConstructSpdyRstStream(1, RST_STREAM_CANCEL));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*rst, 3),
  };

  const char* const kExtraResponseHeaders[] = {"header-name", "header-value"};

  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(kExtraResponseHeaders, 1, 1));

  std::unique_ptr<SpdySerializedFrame> response_body_frame(
      spdy_util_.ConstructSpdyBodyFrame(1, false));

  MockRead reads[] = {
      CreateMockRead(*resp, 1), CreateMockRead(*response_body_frame, 2),
      MockRead(ASYNC, 0, 4),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "GET";
  request_info->url = GURL("https://www.example.org/");
  request_info->priority = LOWEST;
  request_info->end_stream_on_headers = true;

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  std::unique_ptr<CancelOrDeleteStreamDelegate> delegate(
      new CancelOrDeleteStreamDelegate(
          read_buffer.get(), kReadBufferSize,
          CancelOrDeleteStreamDelegate::Phase::ON_DATA_READ, GetParam()));
  delegate->SetRunUntilCompletion(true);
  delegate->Start(std::move(request_info), http_session_.get());
  // Makes sure delegate does not get called.
  base::RunLoop().RunUntilIdle();
  const SpdyHeaderBlock response_headers = delegate->response_headers();
  EXPECT_EQ("200", response_headers.find(":status")->second);
  EXPECT_EQ("header-value", response_headers.find("header-name")->second);
  EXPECT_EQ(kUploadDataSize * 1,
            static_cast<int>(delegate->data_received().size()));
  EXPECT_EQ(0, delegate->on_data_sent_count());

  // If stream is destroyed, do not call into stream.
  if (!GetParam())
    return;
  EXPECT_EQ(0, delegate->GetTotalSentBytes());
  EXPECT_EQ(0, delegate->GetTotalReceivedBytes());
  EXPECT_EQ(kProtoUnknown, delegate->GetProtocol());
}

TEST_P(BidirectionalStreamTest, CancelOrDeleteStreamDuringOnTrailersReceived) {
  std::unique_ptr<SpdySerializedFrame> req(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));

  std::unique_ptr<SpdySerializedFrame> rst(
      spdy_util_.ConstructSpdyRstStream(1, RST_STREAM_CANCEL));
  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*rst, 4),
  };

  const char* const kExtraResponseHeaders[] = {"header-name", "header-value"};

  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(kExtraResponseHeaders, 1, 1));

  std::unique_ptr<SpdySerializedFrame> response_body_frame(
      spdy_util_.ConstructSpdyBodyFrame(1, false));

  SpdyHeaderBlock trailers;
  trailers["foo"] = "bar";
  std::unique_ptr<SpdySerializedFrame> response_trailers(
      spdy_util_.ConstructSpdyResponseHeaders(1, trailers, true));

  MockRead reads[] = {
      CreateMockRead(*resp, 1), CreateMockRead(*response_body_frame, 2),
      CreateMockRead(*response_trailers, 3), MockRead(ASYNC, 0, 5),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "GET";
  request_info->url = GURL("https://www.example.org/");
  request_info->priority = LOWEST;
  request_info->end_stream_on_headers = true;

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  std::unique_ptr<CancelOrDeleteStreamDelegate> delegate(
      new CancelOrDeleteStreamDelegate(
          read_buffer.get(), kReadBufferSize,
          CancelOrDeleteStreamDelegate::Phase::ON_TRAILERS_RECEIVED,
          GetParam()));
  delegate->SetRunUntilCompletion(true);
  delegate->Start(std::move(request_info), http_session_.get());
  // Makes sure delegate does not get called.
  base::RunLoop().RunUntilIdle();
  const SpdyHeaderBlock response_headers = delegate->response_headers();
  EXPECT_EQ("200", response_headers.find(":status")->second);
  EXPECT_EQ("header-value", response_headers.find("header-name")->second);
  EXPECT_EQ("bar", delegate->trailers().find("foo")->second);
  EXPECT_EQ(0, delegate->on_data_sent_count());
  // OnDataRead may or may not have been fired before the stream is
  // canceled/deleted.

  // If stream is destroyed, do not call into stream.
  if (!GetParam())
    return;
  EXPECT_EQ(0, delegate->GetTotalSentBytes());
  EXPECT_EQ(0, delegate->GetTotalReceivedBytes());
  EXPECT_EQ(kProtoUnknown, delegate->GetProtocol());
}

TEST_P(BidirectionalStreamTest, CancelOrDeleteStreamDuringOnFailed) {
  std::unique_ptr<SpdySerializedFrame> req(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));

  std::unique_ptr<SpdySerializedFrame> rst(
      spdy_util_.ConstructSpdyRstStream(1, RST_STREAM_PROTOCOL_ERROR));

  MockWrite writes[] = {
      CreateMockWrite(*req, 0), CreateMockWrite(*rst, 2),
  };

  const char* const kExtraHeaders[] = {"X-UpperCase", "yes"};
  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(kExtraHeaders, 1, 1));

  MockRead reads[] = {
      CreateMockRead(*resp, 1), MockRead(ASYNC, 0, 3),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "GET";
  request_info->url = GURL("https://www.example.org/");
  request_info->priority = LOWEST;
  request_info->end_stream_on_headers = true;

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  std::unique_ptr<CancelOrDeleteStreamDelegate> delegate(
      new CancelOrDeleteStreamDelegate(
          read_buffer.get(), kReadBufferSize,
          CancelOrDeleteStreamDelegate::Phase::ON_FAILED, GetParam()));
  delegate->SetRunUntilCompletion(true);
  delegate->Start(std::move(request_info), http_session_.get());
  // Makes sure delegate does not get called.
  base::RunLoop().RunUntilIdle();
  EXPECT_EQ(delegate->response_headers().end(),
            delegate->response_headers().find(":status"));
  EXPECT_EQ(0, delegate->on_data_sent_count());
  EXPECT_EQ(0, delegate->on_data_read_count());
  EXPECT_EQ(ERR_SPDY_PROTOCOL_ERROR, delegate->error());

  // If stream is destroyed, do not call into stream.
  if (!GetParam())
    return;
  EXPECT_EQ(0, delegate->GetTotalSentBytes());
  EXPECT_EQ(0, delegate->GetTotalReceivedBytes());
  EXPECT_EQ(kProtoUnknown, delegate->GetProtocol());
}

TEST_F(BidirectionalStreamTest, TestHonorAlternativeServiceHeader) {
  std::unique_ptr<SpdySerializedFrame> req(
      spdy_util_.ConstructSpdyGet("https://www.example.org", 1, LOWEST));
  // Empty DATA frame with an END_STREAM flag.
  std::unique_ptr<SpdySerializedFrame> end_stream(
      spdy_util_.ConstructSpdyBodyFrame(1, nullptr, 0, true));

  MockWrite writes[] = {CreateMockWrite(*req.get(), 0)};

  std::string alt_svc_header_value = AlternateProtocolToString(QUIC);
  alt_svc_header_value.append("=\"www.example.org:443\"");
  const char* const kExtraResponseHeaders[] = {"alt-svc",
                                               alt_svc_header_value.c_str()};

  std::unique_ptr<SpdySerializedFrame> resp(
      spdy_util_.ConstructSpdyGetSynReply(kExtraResponseHeaders, 1, 1));
  std::unique_ptr<SpdySerializedFrame> body_frame(
      spdy_util_.ConstructSpdyBodyFrame(1, true));

  MockRead reads[] = {
      CreateMockRead(*resp, 1), CreateMockRead(*body_frame, 2),
      MockRead(SYNCHRONOUS, 0, 3),
  };

  HostPortPair host_port_pair("www.example.org", 443);
  url::SchemeHostPort server("https", "www.example.org", 443);
  SpdySessionKey key(host_port_pair, ProxyServer::Direct(),
                     PRIVACY_MODE_DISABLED);
  session_deps_.parse_alternative_services = true;
  // Enable QUIC so that the alternative service header can be added to
  // HttpServerProperties.
  session_deps_.enable_quic = true;
  InitSession(reads, arraysize(reads), writes, arraysize(writes), key);

  std::unique_ptr<BidirectionalStreamRequestInfo> request_info(
      new BidirectionalStreamRequestInfo);
  request_info->method = "GET";
  request_info->url = GURL("https://www.example.org/");
  request_info->priority = LOWEST;
  request_info->end_stream_on_headers = true;

  scoped_refptr<IOBuffer> read_buffer(new IOBuffer(kReadBufferSize));
  MockTimer* timer = new MockTimer();
  std::unique_ptr<TestDelegateBase> delegate(new TestDelegateBase(
      read_buffer.get(), kReadBufferSize, base::WrapUnique(timer)));
  delegate->SetRunUntilCompletion(true);
  delegate->Start(std::move(request_info), http_session_.get());

  const SpdyHeaderBlock response_headers = delegate->response_headers();
  EXPECT_EQ("200", response_headers.find(":status")->second);
  EXPECT_EQ(alt_svc_header_value, response_headers.find("alt-svc")->second);
  EXPECT_EQ(0, delegate->on_data_sent_count());
  EXPECT_EQ(kProtoHTTP2, delegate->GetProtocol());
  EXPECT_EQ(kUploadData, delegate->data_received());
  EXPECT_EQ(CountWriteBytes(writes, arraysize(writes)),
            delegate->GetTotalSentBytes());
  EXPECT_EQ(CountReadBytes(reads, arraysize(reads)),
            delegate->GetTotalReceivedBytes());

  AlternativeServiceVector alternative_service_vector =
      http_session_->http_server_properties()->GetAlternativeServices(server);
  ASSERT_EQ(1u, alternative_service_vector.size());
  EXPECT_EQ(AlternateProtocolFromNextProto(kProtoQUIC1SPDY3),
            alternative_service_vector[0].protocol);
  EXPECT_EQ("www.example.org", alternative_service_vector[0].host);
  EXPECT_EQ(443, alternative_service_vector[0].port);
}

}  // namespace net
