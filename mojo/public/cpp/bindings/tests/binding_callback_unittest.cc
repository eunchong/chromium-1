// Copyright 2015 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdint.h>

#include "base/bind.h"
#include "base/logging.h"
#include "base/message_loop/message_loop.h"
#include "base/run_loop.h"
#include "build/build_config.h"
#include "mojo/public/cpp/bindings/binding.h"
#include "mojo/public/cpp/bindings/interface_ptr.h"
#include "mojo/public/cpp/bindings/string.h"
#include "mojo/public/cpp/system/message_pipe.h"
#include "mojo/public/cpp/test_support/test_support.h"
#include "mojo/public/interfaces/bindings/tests/sample_interfaces.mojom.h"
#include "testing/gtest/include/gtest/gtest.h"

///////////////////////////////////////////////////////////////////////////////
//
// The tests in this file are designed to test the interaction between a
// Callback and its associated Binding. If a Callback is deleted before
// being used we DCHECK fail--unless the associated Binding has already
// been closed or deleted. This contract must be explained to the Mojo
// application developer. For example it is the developer's responsibility to
// ensure that the Binding is destroyed before an unused Callback is destroyed.
//
///////////////////////////////////////////////////////////////////////////////

namespace mojo {
namespace test {
namespace {

// A Runnable object that saves the last value it sees via the
// provided int32_t*. Used on the client side.
class ValueSaver {
 public:
  ValueSaver(int32_t* last_value_seen, const base::Closure& closure)
      : last_value_seen_(last_value_seen), closure_(closure) {}
  void Run(int32_t x) const {
    *last_value_seen_ = x;
    if (!closure_.is_null()) {
      closure_.Run();
      closure_.Reset();
    }
  }

 private:
  int32_t* const last_value_seen_;
  mutable base::Closure closure_;
};

// An implementation of sample::Provider used on the server side.
// It only implements one of the methods: EchoInt().
// All it does is save the values and Callbacks it sees.
class InterfaceImpl : public sample::Provider {
 public:
  InterfaceImpl()
      : last_server_value_seen_(0),
        callback_saved_(new Callback<void(int32_t)>()) {}

  ~InterfaceImpl() override {
    if (callback_saved_) {
      delete callback_saved_;
    }
  }

  // Run's the callback previously saved from the last invocation
  // of |EchoInt()|.
  bool RunCallback() {
    if (callback_saved_) {
      callback_saved_->Run(last_server_value_seen_);
      return true;
    }
    return false;
  }

  // Delete's the previously saved callback.
  void DeleteCallback() {
    delete callback_saved_;
    callback_saved_ = nullptr;
  }

  // sample::Provider implementation

  // Saves its two input values in member variables and does nothing else.
  void EchoInt(int32_t x, const Callback<void(int32_t)>& callback) override {
    last_server_value_seen_ = x;
    *callback_saved_ = callback;
    if (!closure_.is_null()) {
      closure_.Run();
      closure_.Reset();
    }
  }

  void EchoString(const String& a,
                  const Callback<void(String)>& callback) override {
    CHECK(false) << "Not implemented.";
  }

  void EchoStrings(const String& a,
                   const String& b,
                   const Callback<void(String, String)>& callback) override {
    CHECK(false) << "Not implemented.";
  }

  void EchoMessagePipeHandle(
      ScopedMessagePipeHandle a,
      const Callback<void(ScopedMessagePipeHandle)>& callback) override {
    CHECK(false) << "Not implemented.";
  }

  void EchoEnum(sample::Enum a,
                const Callback<void(sample::Enum)>& callback) override {
    CHECK(false) << "Not implemented.";
  }

  void resetLastServerValueSeen() { last_server_value_seen_ = 0; }

  int32_t last_server_value_seen() const { return last_server_value_seen_; }

  void set_closure(const base::Closure& closure) { closure_ = closure; }

 private:
  int32_t last_server_value_seen_;
  Callback<void(int32_t)>* callback_saved_;
  base::Closure closure_;
};

class BindingCallbackTest : public testing::Test {
 public:
  BindingCallbackTest() {}
  ~BindingCallbackTest() override {}

 protected:
  int32_t last_client_callback_value_seen_;
  sample::ProviderPtr interface_ptr_;

  void PumpMessages() { loop_.RunUntilIdle(); }

 private:
  base::MessageLoop loop_;
};

// Tests that the InterfacePtr and the Binding can communicate with each
// other normally.
TEST_F(BindingCallbackTest, Basic) {
  // Create the ServerImpl and the Binding.
  InterfaceImpl server_impl;
  Binding<sample::Provider> binding(&server_impl, GetProxy(&interface_ptr_));

  // Initialize the test values.
  server_impl.resetLastServerValueSeen();
  last_client_callback_value_seen_ = 0;

  // Invoke the Echo method.
  base::RunLoop run_loop, run_loop2;
  server_impl.set_closure(run_loop.QuitClosure());
  interface_ptr_->EchoInt(
      7,
      ValueSaver(&last_client_callback_value_seen_, run_loop2.QuitClosure()));
  run_loop.Run();

  // Check that server saw the correct value, but the client has not yet.
  EXPECT_EQ(7, server_impl.last_server_value_seen());
  EXPECT_EQ(0, last_client_callback_value_seen_);

  // Now run the Callback.
  server_impl.RunCallback();
  run_loop2.Run();

  // Check that the client has now seen the correct value.
  EXPECT_EQ(7, last_client_callback_value_seen_);

  // Initialize the test values again.
  server_impl.resetLastServerValueSeen();
  last_client_callback_value_seen_ = 0;

  // Invoke the Echo method again.
  base::RunLoop run_loop3, run_loop4;
  server_impl.set_closure(run_loop3.QuitClosure());
  interface_ptr_->EchoInt(
      13,
      ValueSaver(&last_client_callback_value_seen_, run_loop4.QuitClosure()));
  run_loop3.Run();

  // Check that server saw the correct value, but the client has not yet.
  EXPECT_EQ(13, server_impl.last_server_value_seen());
  EXPECT_EQ(0, last_client_callback_value_seen_);

  // Now run the Callback again.
  server_impl.RunCallback();
  run_loop4.Run();

  // Check that the client has now seen the correct value again.
  EXPECT_EQ(13, last_client_callback_value_seen_);
}

// Tests that running the Callback after the Binding has been deleted
// results in a clean failure.
TEST_F(BindingCallbackTest, DeleteBindingThenRunCallback) {
  // Create the ServerImpl.
  InterfaceImpl server_impl;
  base::RunLoop run_loop;
  {
    // Create the binding in an inner scope so it can be deleted first.
    Binding<sample::Provider> binding(&server_impl, GetProxy(&interface_ptr_));
    interface_ptr_.set_connection_error_handler(run_loop.QuitClosure());

    // Initialize the test values.
    server_impl.resetLastServerValueSeen();
    last_client_callback_value_seen_ = 0;

    // Invoke the Echo method.
    base::RunLoop run_loop2;
    server_impl.set_closure(run_loop2.QuitClosure());
    interface_ptr_->EchoInt(
        7,
        ValueSaver(&last_client_callback_value_seen_, base::Closure()));
    run_loop2.Run();
  }
  // The binding has now been destroyed and the pipe is closed.

  // Check that server saw the correct value, but the client has not yet.
  EXPECT_EQ(7, server_impl.last_server_value_seen());
  EXPECT_EQ(0, last_client_callback_value_seen_);

  // Now try to run the Callback. This should do nothing since the pipe
  // is closed.
  EXPECT_TRUE(server_impl.RunCallback());
  PumpMessages();

  // Check that the client has still not seen the correct value.
  EXPECT_EQ(0, last_client_callback_value_seen_);

  // Attempt to invoke the method again and confirm that an error was
  // encountered.
  interface_ptr_->EchoInt(
      13,
      ValueSaver(&last_client_callback_value_seen_, base::Closure()));
  run_loop.Run();
  EXPECT_TRUE(interface_ptr_.encountered_error());
}

// Tests that deleting a Callback without running it after the corresponding
// binding has already been deleted does not result in a crash.
TEST_F(BindingCallbackTest, DeleteBindingThenDeleteCallback) {
  // Create the ServerImpl.
  InterfaceImpl server_impl;
  {
    // Create the binding in an inner scope so it can be deleted first.
    Binding<sample::Provider> binding(&server_impl, GetProxy(&interface_ptr_));

    // Initialize the test values.
    server_impl.resetLastServerValueSeen();
    last_client_callback_value_seen_ = 0;

    // Invoke the Echo method.
    base::RunLoop run_loop;
    server_impl.set_closure(run_loop.QuitClosure());
    interface_ptr_->EchoInt(
        7,
        ValueSaver(&last_client_callback_value_seen_, base::Closure()));
    run_loop.Run();
  }
  // The binding has now been destroyed and the pipe is closed.

  // Check that server saw the correct value, but the client has not yet.
  EXPECT_EQ(7, server_impl.last_server_value_seen());
  EXPECT_EQ(0, last_client_callback_value_seen_);

  // Delete the callback without running it. This should not
  // cause a problem because the insfrastructure can detect that the
  // binding has already been destroyed and the pipe is closed.
  server_impl.DeleteCallback();
}

// Tests that closing a Binding allows us to delete a callback
// without running it without encountering a crash.
TEST_F(BindingCallbackTest, CloseBindingBeforeDeletingCallback) {
  // Create the ServerImpl and the Binding.
  InterfaceImpl server_impl;
  Binding<sample::Provider> binding(&server_impl, GetProxy(&interface_ptr_));

  // Initialize the test values.
  server_impl.resetLastServerValueSeen();
  last_client_callback_value_seen_ = 0;

  // Invoke the Echo method.
  base::RunLoop run_loop;
  server_impl.set_closure(run_loop.QuitClosure());
  interface_ptr_->EchoInt(
      7,
      ValueSaver(&last_client_callback_value_seen_, base::Closure()));
  run_loop.Run();

  // Check that server saw the correct value, but the client has not yet.
  EXPECT_EQ(7, server_impl.last_server_value_seen());
  EXPECT_EQ(0, last_client_callback_value_seen_);

  // Now close the Binding.
  binding.Close();

  // Delete the callback without running it. This should not
  // cause a crash because the insfrastructure can detect that the
  // binding has already been closed.
  server_impl.DeleteCallback();

  // Check that the client has still not seen the correct value.
  EXPECT_EQ(0, last_client_callback_value_seen_);
}

// Tests that deleting a Callback without using it before the
// Binding has been destroyed or closed results in a DCHECK.
TEST_F(BindingCallbackTest, DeleteCallbackBeforeBindingDeathTest) {
  // Create the ServerImpl and the Binding.
  InterfaceImpl server_impl;
  Binding<sample::Provider> binding(&server_impl, GetProxy(&interface_ptr_));

  // Initialize the test values.
  server_impl.resetLastServerValueSeen();
  last_client_callback_value_seen_ = 0;

  // Invoke the Echo method.
  base::RunLoop run_loop;
  server_impl.set_closure(run_loop.QuitClosure());
  interface_ptr_->EchoInt(
      7,
      ValueSaver(&last_client_callback_value_seen_, base::Closure()));
  run_loop.Run();

  // Check that server saw the correct value, but the client has not yet.
  EXPECT_EQ(7, server_impl.last_server_value_seen());
  EXPECT_EQ(0, last_client_callback_value_seen_);

#if !defined(NDEBUG) || defined(DCHECK_ALWAYS_ON)
  // Delete the callback without running it. This should cause a crash in debug
  // builds due to a DCHECK.
  std::string regex("Check failed: !is_valid");
#if defined(OS_WIN)
  // TODO(msw): Fix MOJO_DCHECK logs and EXPECT_DEATH* on Win: crbug.com/535014
  regex.clear();
#endif  // OS_WIN
  EXPECT_DEATH_IF_SUPPORTED(server_impl.DeleteCallback(), regex.c_str());
#endif  // !defined(NDEBUG) || defined(DCHECK_ALWAYS_ON)
}

}  // namespace
}  // namespace test
}  // namespace mojo
