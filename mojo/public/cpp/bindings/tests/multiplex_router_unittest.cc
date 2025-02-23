// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "mojo/public/cpp/bindings/lib/multiplex_router.h"

#include <utility>

#include "base/memory/ptr_util.h"
#include "base/memory/ref_counted.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "base/threading/thread_task_runner_handle.h"
#include "mojo/public/cpp/bindings/lib/interface_endpoint_client.h"
#include "mojo/public/cpp/bindings/lib/scoped_interface_endpoint_handle.h"
#include "mojo/public/cpp/bindings/message.h"
#include "mojo/public/cpp/bindings/message_filter.h"
#include "mojo/public/cpp/bindings/tests/message_queue.h"
#include "mojo/public/cpp/bindings/tests/router_test_util.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace mojo {
namespace test {
namespace {

using mojo::internal::InterfaceEndpointClient;
using mojo::internal::MultiplexRouter;
using mojo::internal::ScopedInterfaceEndpointHandle;

class MultiplexRouterTest : public testing::Test {
 public:
  MultiplexRouterTest() {}

  void SetUp() override {
    MessagePipe pipe;
    router0_ = new MultiplexRouter(true, std::move(pipe.handle0),
                                   base::ThreadTaskRunnerHandle::Get());
    router1_ = new MultiplexRouter(true, std::move(pipe.handle1),
                                   base::ThreadTaskRunnerHandle::Get());
    router0_->CreateEndpointHandlePair(&endpoint0_, &endpoint1_);
    endpoint1_ =
        EmulatePassingEndpointHandle(std::move(endpoint1_), router1_.get());
  }

  void TearDown() override {}

  void PumpMessages() { loop_.RunUntilIdle(); }

  ScopedInterfaceEndpointHandle EmulatePassingEndpointHandle(
      ScopedInterfaceEndpointHandle handle,
      MultiplexRouter* target) {
    CHECK(!handle.is_local());

    return target->CreateLocalEndpointHandle(handle.release());
  }

 protected:
  scoped_refptr<MultiplexRouter> router0_;
  scoped_refptr<MultiplexRouter> router1_;
  ScopedInterfaceEndpointHandle endpoint0_;
  ScopedInterfaceEndpointHandle endpoint1_;

 private:
  base::MessageLoop loop_;
};

TEST_F(MultiplexRouterTest, BasicRequestResponse) {
  InterfaceEndpointClient client0(std::move(endpoint0_), nullptr,
                                  base::WrapUnique(new PassThroughFilter()),
                                  false, base::ThreadTaskRunnerHandle::Get());
  ResponseGenerator generator;
  InterfaceEndpointClient client1(std::move(endpoint1_), &generator,
                                  base::WrapUnique(new PassThroughFilter()),
                                  false, base::ThreadTaskRunnerHandle::Get());

  Message request;
  AllocRequestMessage(1, "hello", &request);

  MessageQueue message_queue;
  base::RunLoop run_loop;
  client0.AcceptWithResponder(
      &request,
      new MessageAccumulator(&message_queue, run_loop.QuitClosure()));

  run_loop.Run();

  EXPECT_FALSE(message_queue.IsEmpty());

  Message response;
  message_queue.Pop(&response);

  EXPECT_EQ(std::string("hello world!"),
            std::string(reinterpret_cast<const char*>(response.payload())));

  // Send a second message on the pipe.
  Message request2;
  AllocRequestMessage(1, "hello again", &request2);

  base::RunLoop run_loop2;
  client0.AcceptWithResponder(
      &request2,
      new MessageAccumulator(&message_queue, run_loop2.QuitClosure()));

  run_loop2.Run();

  EXPECT_FALSE(message_queue.IsEmpty());

  message_queue.Pop(&response);

  EXPECT_EQ(std::string("hello again world!"),
            std::string(reinterpret_cast<const char*>(response.payload())));
}

TEST_F(MultiplexRouterTest, BasicRequestResponse_Synchronous) {
  InterfaceEndpointClient client0(std::move(endpoint0_), nullptr,
                                  base::WrapUnique(new PassThroughFilter()),
                                  false, base::ThreadTaskRunnerHandle::Get());
  ResponseGenerator generator;
  InterfaceEndpointClient client1(std::move(endpoint1_), &generator,
                                  base::WrapUnique(new PassThroughFilter()),
                                  false, base::ThreadTaskRunnerHandle::Get());

  Message request;
  AllocRequestMessage(1, "hello", &request);

  MessageQueue message_queue;
  client0.AcceptWithResponder(&request, new MessageAccumulator(&message_queue));

  router1_->WaitForIncomingMessage(MOJO_DEADLINE_INDEFINITE);
  router0_->WaitForIncomingMessage(MOJO_DEADLINE_INDEFINITE);

  EXPECT_FALSE(message_queue.IsEmpty());

  Message response;
  message_queue.Pop(&response);

  EXPECT_EQ(std::string("hello world!"),
            std::string(reinterpret_cast<const char*>(response.payload())));

  // Send a second message on the pipe.
  Message request2;
  AllocRequestMessage(1, "hello again", &request2);

  client0.AcceptWithResponder(&request2,
                              new MessageAccumulator(&message_queue));

  router1_->WaitForIncomingMessage(MOJO_DEADLINE_INDEFINITE);
  router0_->WaitForIncomingMessage(MOJO_DEADLINE_INDEFINITE);

  EXPECT_FALSE(message_queue.IsEmpty());

  message_queue.Pop(&response);

  EXPECT_EQ(std::string("hello again world!"),
            std::string(reinterpret_cast<const char*>(response.payload())));
}

TEST_F(MultiplexRouterTest, RequestWithNoReceiver) {
  InterfaceEndpointClient client0(std::move(endpoint0_), nullptr,
                                  base::WrapUnique(new PassThroughFilter()),
                                  false, base::ThreadTaskRunnerHandle::Get());
  InterfaceEndpointClient client1(std::move(endpoint1_), nullptr,
                                  base::WrapUnique(new PassThroughFilter()),
                                  false, base::ThreadTaskRunnerHandle::Get());

  // Without an incoming receiver set on client1, we expect client0 to observe
  // an error as a result of sending a message.

  Message request;
  AllocRequestMessage(1, "hello", &request);

  MessageQueue message_queue;
  base::RunLoop run_loop, run_loop2;
  client0.set_connection_error_handler(run_loop.QuitClosure());
  client1.set_connection_error_handler(run_loop2.QuitClosure());
  client0.AcceptWithResponder(
      &request, new MessageAccumulator(&message_queue, run_loop.QuitClosure()));

  run_loop.Run();
  run_loop2.Run();

  EXPECT_TRUE(client0.encountered_error());
  EXPECT_TRUE(client1.encountered_error());
  EXPECT_TRUE(message_queue.IsEmpty());
}

// Tests MultiplexRouter using the LazyResponseGenerator. The responses will not
// be sent until after the requests have been accepted.
TEST_F(MultiplexRouterTest, LazyResponses) {
  InterfaceEndpointClient client0(std::move(endpoint0_), nullptr,
                                  base::WrapUnique(new PassThroughFilter()),
                                  false, base::ThreadTaskRunnerHandle::Get());
  base::RunLoop run_loop;
  LazyResponseGenerator generator(run_loop.QuitClosure());
  InterfaceEndpointClient client1(std::move(endpoint1_), &generator,
                                  base::WrapUnique(new PassThroughFilter()),
                                  false, base::ThreadTaskRunnerHandle::Get());

  Message request;
  AllocRequestMessage(1, "hello", &request);

  MessageQueue message_queue;
  base::RunLoop run_loop2;
  client0.AcceptWithResponder(
      &request,
      new MessageAccumulator(&message_queue, run_loop2.QuitClosure()));
  run_loop.Run();

  // The request has been received but the response has not been sent yet.
  EXPECT_TRUE(message_queue.IsEmpty());

  // Send the response.
  EXPECT_TRUE(generator.responder_is_valid());
  generator.CompleteWithResponse();
  run_loop2.Run();

  // Check the response.
  EXPECT_FALSE(message_queue.IsEmpty());
  Message response;
  message_queue.Pop(&response);
  EXPECT_EQ(std::string("hello world!"),
            std::string(reinterpret_cast<const char*>(response.payload())));

  // Send a second message on the pipe.
  base::RunLoop run_loop3;
  generator.set_closure(run_loop3.QuitClosure());
  Message request2;
  AllocRequestMessage(1, "hello again", &request2);

  base::RunLoop run_loop4;
  client0.AcceptWithResponder(
      &request2,
      new MessageAccumulator(&message_queue, run_loop4.QuitClosure()));
  run_loop3.Run();

  // The request has been received but the response has not been sent yet.
  EXPECT_TRUE(message_queue.IsEmpty());

  // Send the second response.
  EXPECT_TRUE(generator.responder_is_valid());
  generator.CompleteWithResponse();
  run_loop4.Run();

  // Check the second response.
  EXPECT_FALSE(message_queue.IsEmpty());
  message_queue.Pop(&response);
  EXPECT_EQ(std::string("hello again world!"),
            std::string(reinterpret_cast<const char*>(response.payload())));
}

// Tests that if the receiving application destroys the responder_ without
// sending a response, then we trigger connection error at both sides. Moreover,
// both sides still appear to have a valid message pipe handle bound.
TEST_F(MultiplexRouterTest, MissingResponses) {
  base::RunLoop run_loop0, run_loop1;
  InterfaceEndpointClient client0(std::move(endpoint0_), nullptr,
                                  base::WrapUnique(new PassThroughFilter()),
                                  false, base::ThreadTaskRunnerHandle::Get());
  bool error_handler_called0 = false;
  client0.set_connection_error_handler(
      [&error_handler_called0, &run_loop0]() {
        error_handler_called0 = true;
        run_loop0.Quit();
      });

  base::RunLoop run_loop3;
  LazyResponseGenerator generator(run_loop3.QuitClosure());
  InterfaceEndpointClient client1(std::move(endpoint1_), &generator,
                                  base::WrapUnique(new PassThroughFilter()),
                                  false, base::ThreadTaskRunnerHandle::Get());
  bool error_handler_called1 = false;
  client1.set_connection_error_handler(
      [&error_handler_called1, &run_loop1]() {
        error_handler_called1 = true;
        run_loop1.Quit();
      });

  Message request;
  AllocRequestMessage(1, "hello", &request);

  MessageQueue message_queue;
  client0.AcceptWithResponder(&request, new MessageAccumulator(&message_queue));
  run_loop3.Run();

  // The request has been received but no response has been sent.
  EXPECT_TRUE(message_queue.IsEmpty());

  // Destroy the responder MessagerReceiver but don't send any response.
  generator.CompleteWithoutResponse();
  run_loop0.Run();
  run_loop1.Run();

  // Check that no response was received.
  EXPECT_TRUE(message_queue.IsEmpty());

  // Connection error handler is called at both sides.
  EXPECT_TRUE(error_handler_called0);
  EXPECT_TRUE(error_handler_called1);

  // The error flag is set at both sides.
  EXPECT_TRUE(client0.encountered_error());
  EXPECT_TRUE(client1.encountered_error());

  // The message pipe handle is valid at both sides.
  EXPECT_TRUE(router0_->is_valid());
  EXPECT_TRUE(router1_->is_valid());
}

TEST_F(MultiplexRouterTest, LateResponse) {
  // Test that things won't blow up if we try to send a message to a
  // MessageReceiver, which was given to us via AcceptWithResponder,
  // after the router has gone away.

  base::RunLoop run_loop;
  LazyResponseGenerator generator(run_loop.QuitClosure());
  {
    InterfaceEndpointClient client0(std::move(endpoint0_), nullptr,
                                    base::WrapUnique(new PassThroughFilter()),
                                    false, base::ThreadTaskRunnerHandle::Get());
    InterfaceEndpointClient client1(std::move(endpoint1_), &generator,
                                    base::WrapUnique(new PassThroughFilter()),
                                    false, base::ThreadTaskRunnerHandle::Get());

    Message request;
    AllocRequestMessage(1, "hello", &request);

    MessageQueue message_queue;
    client0.AcceptWithResponder(&request,
                                new MessageAccumulator(&message_queue));

    run_loop.Run();

    EXPECT_TRUE(generator.has_responder());
  }

  EXPECT_FALSE(generator.responder_is_valid());
  generator.CompleteWithResponse();  // This should end up doing nothing.
}

// TODO(yzshen): add more tests.

}  // namespace
}  // namespace test
}  // namespace mojo
