// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>

#include <utility>

#include "base/bind_helpers.h"
#include "base/command_line.h"
#include "base/macros.h"
#include "base/memory/ptr_util.h"
#include "base/run_loop.h"
#include "base/strings/string_number_conversions.h"
#include "base/strings/string_util.h"
#include "base/strings/utf_string_conversions.h"
#include "base/values.h"
#include "build/build_config.h"
#include "chrome/browser/extensions/dev_mode_bubble_delegate.h"
#include "chrome/browser/extensions/extension_action_test_util.h"
#include "chrome/browser/extensions/extension_function_test_utils.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/extensions/extension_web_ui_override_registrar.h"
#include "chrome/browser/extensions/ntp_overridden_bubble_delegate.h"
#include "chrome/browser/extensions/proxy_overridden_bubble_delegate.h"
#include "chrome/browser/extensions/settings_api_bubble_delegate.h"
#include "chrome/browser/extensions/suspicious_extension_bubble_delegate.h"
#include "chrome/browser/extensions/test_extension_system.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model.h"
#include "chrome/browser/ui/toolbar/toolbar_actions_model_factory.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "chrome/test/base/testing_profile.h"
#include "components/proxy_config/proxy_config_pref_names.h"
#include "content/public/test/test_browser_thread_bundle.h"
#include "extensions/browser/api_test_utils.h"
#include "extensions/browser/extension_pref_value_map.h"
#include "extensions/browser/extension_pref_value_map_factory.h"
#include "extensions/browser/extension_prefs.h"
#include "extensions/browser/extension_registry.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/uninstall_reason.h"
#include "extensions/common/extension.h"
#include "extensions/common/extension_builder.h"
#include "extensions/common/feature_switch.h"
#include "extensions/common/value_builder.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const char kId1[] = "iccfkkhkfiphcjdakkmcjmkfboccmndk";
const char kId2[] = "ajjhifimiemdpmophmkkkcijegphclbl";
const char kId3[] = "ioibbbfddncmmabjmpokikkeiofalaek";

std::unique_ptr<KeyedService> BuildOverrideRegistrar(
    content::BrowserContext* context) {
  return base::WrapUnique(
      new extensions::ExtensionWebUIOverrideRegistrar(context));
}

// Creates a new ToolbarActionsModel for the given |context|.
std::unique_ptr<KeyedService> BuildToolbarModel(
    content::BrowserContext* context) {
  return base::WrapUnique(
      new ToolbarActionsModel(Profile::FromBrowserContext(context),
                              extensions::ExtensionPrefs::Get(context)));
}

}  // namespace

namespace extensions {

class TestExtensionMessageBubbleController :
    public ExtensionMessageBubbleController {
 public:
  TestExtensionMessageBubbleController(
      ExtensionMessageBubbleController::Delegate* delegate,
      Browser* browser)
      : ExtensionMessageBubbleController(delegate, browser),
        action_button_callback_count_(0),
        dismiss_button_callback_count_(0),
        link_click_callback_count_(0) {}
  ~TestExtensionMessageBubbleController() override {}

  // ExtensionMessageBubbleController:
  void OnBubbleAction() override {
    ++action_button_callback_count_;
    ExtensionMessageBubbleController::OnBubbleAction();
  }
  void OnBubbleDismiss(bool by_deactivation) override {
    ++dismiss_button_callback_count_;
    ExtensionMessageBubbleController::OnBubbleDismiss(by_deactivation);
  }
  void OnLinkClicked() override {
    ++link_click_callback_count_;
    ExtensionMessageBubbleController::OnLinkClicked();
  }

  size_t action_click_count() { return action_button_callback_count_; }
  size_t dismiss_click_count() { return dismiss_button_callback_count_; }
  size_t link_click_count() { return link_click_callback_count_; }

 private:
  // How often each button has been called.
  size_t action_button_callback_count_;
  size_t dismiss_button_callback_count_;
  size_t link_click_callback_count_;

  DISALLOW_COPY_AND_ASSIGN(TestExtensionMessageBubbleController);
};

// A fake bubble used for testing the controller. Takes an action that specifies
// what should happen when the bubble is "shown" (the bubble is actually not
// shown, the corresponding action is taken immediately).
class FakeExtensionMessageBubble {
 public:
  enum ExtensionBubbleAction {
    BUBBLE_ACTION_CLICK_ACTION_BUTTON = 0,
    BUBBLE_ACTION_CLICK_DISMISS_BUTTON,
    BUBBLE_ACTION_DISMISS_DEACTIVATION,
    BUBBLE_ACTION_CLICK_LINK,
  };

  FakeExtensionMessageBubble()
      : action_(BUBBLE_ACTION_CLICK_ACTION_BUTTON), controller_(nullptr) {}

  void set_action_on_show(ExtensionBubbleAction action) {
    action_ = action;
  }
  void set_controller(ExtensionMessageBubbleController* controller) {
    controller_ = controller;
  }

  void Show() {
    controller_->OnShown();
    switch (action_) {
      case BUBBLE_ACTION_CLICK_ACTION_BUTTON:
        controller_->OnBubbleAction();
        break;
      case BUBBLE_ACTION_CLICK_DISMISS_BUTTON:
        controller_->OnBubbleDismiss(false);
        break;
      case BUBBLE_ACTION_DISMISS_DEACTIVATION:
        controller_->OnBubbleDismiss(true);
        break;
      case BUBBLE_ACTION_CLICK_LINK:
        controller_->OnLinkClicked();
        break;
    }
  }

 private:
  ExtensionBubbleAction action_;
  ExtensionMessageBubbleController* controller_;

  DISALLOW_COPY_AND_ASSIGN(FakeExtensionMessageBubble);
};

class ExtensionMessageBubbleTest : public BrowserWithTestWindowTest {
 public:
  ExtensionMessageBubbleTest() {}

  testing::AssertionResult LoadGenericExtension(const std::string& index,
                                                const std::string& id,
                                                Manifest::Location location) {
    ExtensionBuilder builder;
    builder.SetManifest(DictionaryBuilder()
                            .Set("name", std::string("Extension " + index))
                            .Set("version", "1.0")
                            .Set("manifest_version", 2)
                            .Build());
    builder.SetLocation(location);
    builder.SetID(id);
    service_->AddExtension(builder.Build().get());

    if (ExtensionRegistry::Get(profile())->enabled_extensions().GetByID(id))
      return testing::AssertionSuccess();
    return testing::AssertionFailure() << "Could not install extension: " << id;
  }

  testing::AssertionResult LoadExtensionWithAction(
      const std::string& index,
      const std::string& id,
      Manifest::Location location) {
    ExtensionBuilder builder;
    builder.SetManifest(
        DictionaryBuilder()
            .Set("name", std::string("Extension " + index))
            .Set("version", "1.0")
            .Set("manifest_version", 2)
            .Set("browser_action", DictionaryBuilder()
                                       .Set("default_title", "Default title")
                                       .Build())
            .Build());
    builder.SetLocation(location);
    builder.SetID(id);
    service_->AddExtension(builder.Build().get());

    if (ExtensionRegistry::Get(profile())->enabled_extensions().GetByID(id))
      return testing::AssertionSuccess();
    return testing::AssertionFailure() << "Could not install extension: " << id;
  }

  testing::AssertionResult LoadExtensionOverridingHome(
      const std::string& index,
      const std::string& id,
      Manifest::Location location) {
    ExtensionBuilder builder;
    builder.SetManifest(DictionaryBuilder()
                            .Set("name", std::string("Extension " + index))
                            .Set("version", "1.0")
                            .Set("manifest_version", 2)
                            .Set("chrome_settings_overrides",
                                 DictionaryBuilder()
                                     .Set("homepage", "http://www.google.com")
                                     .Build())
                            .Build());
    builder.SetLocation(location);
    builder.SetID(id);
    service_->AddExtension(builder.Build().get());

    if (ExtensionRegistry::Get(profile())->enabled_extensions().GetByID(id))
      return testing::AssertionSuccess();
    return testing::AssertionFailure() << "Could not install extension: " << id;
  }

  testing::AssertionResult LoadExtensionOverridingStart(
      const std::string& index,
      const std::string& id,
      Manifest::Location location) {
    ExtensionBuilder builder;
    builder.SetManifest(
        DictionaryBuilder()
            .Set("name", std::string("Extension " + index))
            .Set("version", "1.0")
            .Set("manifest_version", 2)
            .Set("chrome_settings_overrides",
                 DictionaryBuilder()
                     .Set("startup_pages",
                          ListBuilder().Append("http://www.google.com").Build())
                     .Build())
            .Build());
    builder.SetLocation(location);
    builder.SetID(id);
    service_->AddExtension(builder.Build().get());

    if (ExtensionRegistry::Get(profile())->enabled_extensions().GetByID(id))
      return testing::AssertionSuccess();
    return testing::AssertionFailure() << "Could not install extension: " << id;
  }

  testing::AssertionResult LoadExtensionOverridingNtp(
      const std::string& index,
      const std::string& id,
      Manifest::Location location) {
    ExtensionBuilder builder;
    builder.SetManifest(
        DictionaryBuilder()
            .Set("name", std::string("Extension " + index))
            .Set("version", "1.0")
            .Set("manifest_version", 2)
            .Set("chrome_url_overrides",
                 DictionaryBuilder().Set("newtab", "Default.html").Build())
            .Build());

    builder.SetLocation(location);
    builder.SetID(id);
    service_->AddExtension(builder.Build().get());

    if (ExtensionRegistry::Get(profile())->enabled_extensions().GetByID(id))
      return testing::AssertionSuccess();
    return testing::AssertionFailure() << "Could not install extension: " << id;
  }

  testing::AssertionResult LoadExtensionOverridingProxy(
      const std::string& index,
      const std::string& id,
      Manifest::Location location) {
    ExtensionBuilder builder;
    builder.SetManifest(
        DictionaryBuilder()
            .Set("name", std::string("Extension " + index))
            .Set("version", "1.0")
            .Set("manifest_version", 2)
            .Set("permissions", ListBuilder().Append("proxy").Build())
            .Build());

    builder.SetLocation(location);
    builder.SetID(id);
    service_->AddExtension(builder.Build().get());

    // The proxy check relies on ExtensionPrefValueMap being up to date as to
    // specifying which extension is controlling the proxy, but unfortunately
    // that Map is not updated automatically for unit tests, so we simulate the
    // update here to avoid test failures.
    ExtensionPrefValueMap* extension_prefs_value_map =
        ExtensionPrefValueMapFactory::GetForBrowserContext(profile());
    extension_prefs_value_map->RegisterExtension(
        id,
        base::Time::Now(),
        true,    // is_enabled.
        false);  // is_incognito_enabled.
    extension_prefs_value_map->SetExtensionPref(id, proxy_config::prefs::kProxy,
                                                kExtensionPrefsScopeRegular,
                                                new base::StringValue(id));

    if (ExtensionRegistry::Get(profile())->enabled_extensions().GetByID(id))
      return testing::AssertionSuccess();
    return testing::AssertionFailure() << "Could not install extension: " << id;
  }

  void Init() {
    // The two lines of magical incantation required to get the extension
    // service to work inside a unit test and access the extension prefs.
    static_cast<TestExtensionSystem*>(ExtensionSystem::Get(profile()))
        ->CreateExtensionService(base::CommandLine::ForCurrentProcess(),
                                 base::FilePath(), false);
    service_ = ExtensionSystem::Get(profile())->extension_service();
    service_->Init();

    extensions::ExtensionWebUIOverrideRegistrar::GetFactoryInstance()->
        SetTestingFactory(profile(), &BuildOverrideRegistrar);
    extensions::ExtensionWebUIOverrideRegistrar::GetFactoryInstance()->Get(
        profile());
  }

  ~ExtensionMessageBubbleTest() override {}

  void SetUp() override {
    BrowserWithTestWindowTest::SetUp();
    command_line_.reset(new base::CommandLine(base::CommandLine::NO_PROGRAM));
    ExtensionMessageBubbleController::set_should_ignore_learn_more_for_testing(
        true);
  }

  void TearDown() override {
    ExtensionMessageBubbleController::set_should_ignore_learn_more_for_testing(
        false);
    BrowserWithTestWindowTest::TearDown();
  }

 protected:
  scoped_refptr<Extension> CreateExtension(
      Manifest::Location location,
      const std::string& data,
      const std::string& id) {
    std::unique_ptr<base::DictionaryValue> parsed_manifest(
        api_test_utils::ParseDictionary(data));
    return api_test_utils::CreateExtension(location, parsed_manifest.get(), id);
  }

  ExtensionService* service_;

 private:
  std::unique_ptr<base::CommandLine> command_line_;

  DISALLOW_COPY_AND_ASSIGN(ExtensionMessageBubbleTest);
};

TEST_F(ExtensionMessageBubbleTest, BubbleReshowsOnDeactivationDismissal) {
  Init();

  ASSERT_TRUE(LoadExtensionOverridingNtp("1", kId1, Manifest::INTERNAL));
  ASSERT_TRUE(LoadExtensionOverridingNtp("2", kId2, Manifest::INTERNAL));
  std::unique_ptr<TestExtensionMessageBubbleController> controller(
      new TestExtensionMessageBubbleController(
          new NtpOverriddenBubbleDelegate(browser()->profile()), browser()));

  // The list will contain one enabled unpacked extension (ext 2).
  EXPECT_TRUE(controller->ShouldShow());
  std::vector<base::string16> override_extensions =
      controller->GetExtensionList();
  ASSERT_EQ(1U, override_extensions.size());
  EXPECT_EQ(base::ASCIIToUTF16("Extension 2"), override_extensions[0]);
  EXPECT_EQ(0U, controller->link_click_count());
  EXPECT_EQ(0U, controller->dismiss_click_count());
  EXPECT_EQ(0U, controller->action_click_count());

  // Simulate showing the bubble and dismissing it due to deactivation.
  FakeExtensionMessageBubble bubble;
  bubble.set_action_on_show(
      FakeExtensionMessageBubble::BUBBLE_ACTION_DISMISS_DEACTIVATION);
  bubble.set_controller(controller.get());
  bubble.Show();
  EXPECT_EQ(0U, controller->link_click_count());
  EXPECT_EQ(0U, controller->action_click_count());
  EXPECT_EQ(1U, controller->dismiss_click_count());

  // No extension should have become disabled.
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId2));
  // And since it was dismissed due to deactivation, the extension should not
  // have been acknowledged.
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId2));

  bubble.set_action_on_show(
      FakeExtensionMessageBubble::BUBBLE_ACTION_DISMISS_DEACTIVATION);
  controller.reset(new TestExtensionMessageBubbleController(
      new NtpOverriddenBubbleDelegate(browser()->profile()), browser()));
  // The bubble shouldn't show again for the same profile (we don't want to
  // be annoying).
  EXPECT_FALSE(controller->ShouldShow());
  controller->ClearProfileListForTesting();
  EXPECT_TRUE(controller->ShouldShow());
  // Explicitly click the dismiss button. The extension should be acknowledged.
  bubble.set_controller(controller.get());
  bubble.set_action_on_show(
      FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_DISMISS_BUTTON);
  bubble.Show();
  EXPECT_TRUE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId2));

  // Uninstall the current ntp-controlling extension, allowing the other to
  // take control.
  service_->UninstallExtension(kId2, UNINSTALL_REASON_FOR_TESTING,
                               base::Bind(&base::DoNothing), nullptr);

  // Even though we already showed for the given profile, we should show again,
  // because it's a different extension.
  controller.reset(new TestExtensionMessageBubbleController(
      new NtpOverriddenBubbleDelegate(browser()->profile()), browser()));
  EXPECT_TRUE(controller->ShouldShow());
}

// The feature this is meant to test is only enacted on Windows, but it should
// pass on all platforms.
TEST_F(ExtensionMessageBubbleTest, WipeoutControllerTest) {
  Init();
  // Add three extensions, and control two of them in this test (extension 1
  // and 2).
  ASSERT_TRUE(LoadExtensionWithAction("1", kId1, Manifest::COMMAND_LINE));
  ASSERT_TRUE(LoadGenericExtension("2", kId2, Manifest::UNPACKED));
  ASSERT_TRUE(LoadGenericExtension("3", kId3, Manifest::EXTERNAL_POLICY));

  std::unique_ptr<TestExtensionMessageBubbleController> controller(
      new TestExtensionMessageBubbleController(
          new SuspiciousExtensionBubbleDelegate(browser()->profile()),
          browser()));
  FakeExtensionMessageBubble bubble;
  bubble.set_action_on_show(
      FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_DISMISS_BUTTON);

  // Validate that we don't have a suppress value for the extensions.
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));

  EXPECT_FALSE(controller->ShouldShow());
  std::vector<base::string16> suspicious_extensions =
      controller->GetExtensionList();
  EXPECT_EQ(0U, suspicious_extensions.size());
  EXPECT_EQ(0U, controller->link_click_count());
  EXPECT_EQ(0U, controller->dismiss_click_count());

  // Now disable an extension, specifying the wipeout flag.
  service_->DisableExtension(kId1, Extension::DISABLE_NOT_VERIFIED);

  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId2));
  controller.reset(
      new TestExtensionMessageBubbleController(
          new SuspiciousExtensionBubbleDelegate(browser()->profile()),
          browser()));
  controller->ClearProfileListForTesting();
  EXPECT_TRUE(controller->ShouldShow());
  suspicious_extensions = controller->GetExtensionList();
  ASSERT_EQ(1U, suspicious_extensions.size());
  EXPECT_TRUE(base::ASCIIToUTF16("Extension 1") == suspicious_extensions[0]);
  bubble.set_controller(controller.get());
  bubble.Show();  // Simulate showing the bubble.
  EXPECT_EQ(0U, controller->link_click_count());
  EXPECT_EQ(1U, controller->dismiss_click_count());
  // Now the acknowledge flag should be set only for the first extension.
  EXPECT_TRUE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId2));
  // Clear the flag.
  controller->delegate()->SetBubbleInfoBeenAcknowledged(kId1, false);
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));

  // Now disable the other extension and exercise the link click code path.
  service_->DisableExtension(kId2, Extension::DISABLE_NOT_VERIFIED);

  bubble.set_action_on_show(
      FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_LINK);
  controller.reset(
      new TestExtensionMessageBubbleController(
          new SuspiciousExtensionBubbleDelegate(browser()->profile()),
          browser()));
  controller->ClearProfileListForTesting();
  EXPECT_TRUE(controller->ShouldShow());
  suspicious_extensions = controller->GetExtensionList();
  ASSERT_EQ(2U, suspicious_extensions.size());
  EXPECT_TRUE(base::ASCIIToUTF16("Extension 1") == suspicious_extensions[1]);
  EXPECT_TRUE(base::ASCIIToUTF16("Extension 2") == suspicious_extensions[0]);
  bubble.set_controller(controller.get());
  bubble.Show();  // Simulate showing the bubble.
  EXPECT_EQ(1U, controller->link_click_count());
  EXPECT_EQ(0U, controller->dismiss_click_count());
  EXPECT_TRUE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));
}

// The feature this is meant to test is only enacted on Windows, but it should
// pass on all platforms.
TEST_F(ExtensionMessageBubbleTest, DevModeControllerTest) {
  FeatureSwitch::ScopedOverride force_dev_mode_highlighting(
      FeatureSwitch::force_dev_mode_highlighting(), true);
  Init();
  // Add three extensions, and control two of them in this test (extension 1
  // and 2). Extension 1 is a regular extension, Extension 2 is UNPACKED so it
  // counts as a DevMode extension.
  ASSERT_TRUE(LoadExtensionWithAction("1", kId1, Manifest::COMMAND_LINE));
  ASSERT_TRUE(LoadGenericExtension("2", kId2, Manifest::UNPACKED));
  ASSERT_TRUE(LoadGenericExtension("3", kId3, Manifest::EXTERNAL_POLICY));

  std::unique_ptr<TestExtensionMessageBubbleController> controller(
      new TestExtensionMessageBubbleController(
          new DevModeBubbleDelegate(browser()->profile()), browser()));

  // The list will contain one enabled unpacked extension.
  EXPECT_TRUE(controller->ShouldShow());
  std::vector<base::string16> dev_mode_extensions =
      controller->GetExtensionList();
  ASSERT_EQ(2U, dev_mode_extensions.size());
  EXPECT_TRUE(base::ASCIIToUTF16("Extension 2") == dev_mode_extensions[0]);
  EXPECT_TRUE(base::ASCIIToUTF16("Extension 1") == dev_mode_extensions[1]);
  EXPECT_EQ(0U, controller->link_click_count());
  EXPECT_EQ(0U, controller->dismiss_click_count());
  EXPECT_EQ(0U, controller->action_click_count());

  // Simulate showing the bubble.
  FakeExtensionMessageBubble bubble;
  bubble.set_action_on_show(
      FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_DISMISS_BUTTON);
  bubble.set_controller(controller.get());
  bubble.Show();
  EXPECT_EQ(0U, controller->link_click_count());
  EXPECT_EQ(0U, controller->action_click_count());
  EXPECT_EQ(1U, controller->dismiss_click_count());
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId1) != NULL);
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId2) != NULL);

  // Do it again, but now press different button (Disable).
  bubble.set_action_on_show(
      FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_ACTION_BUTTON);
  controller.reset(
      new TestExtensionMessageBubbleController(
          new DevModeBubbleDelegate(browser()->profile()),
          browser()));
  // Most bubbles would want to show again as long as the extensions weren't
  // acknowledged and the bubble wasn't dismissed due to deactivation. Since dev
  // mode extensions can't be (persistently) acknowledged, this isn't the case
  // for the dev mode bubble, and we should only show once per profile.
  EXPECT_FALSE(controller->ShouldShow());
  controller->ClearProfileListForTesting();
  EXPECT_TRUE(controller->ShouldShow());
  dev_mode_extensions = controller->GetExtensionList();
  EXPECT_EQ(2U, dev_mode_extensions.size());
  bubble.set_controller(controller.get());
  bubble.Show();  // Simulate showing the bubble.
  EXPECT_EQ(0U, controller->link_click_count());
  EXPECT_EQ(1U, controller->action_click_count());
  EXPECT_EQ(0U, controller->dismiss_click_count());
  EXPECT_TRUE(registry->disabled_extensions().GetByID(kId1) != NULL);
  EXPECT_TRUE(registry->disabled_extensions().GetByID(kId2) != NULL);

  // Re-enable the extensions (disabled by the action button above).
  service_->EnableExtension(kId1);
  service_->EnableExtension(kId2);

  // Show the dialog a third time, but now press the learn more link.
  bubble.set_action_on_show(
      FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_LINK);
  controller.reset(
      new TestExtensionMessageBubbleController(
          new DevModeBubbleDelegate(browser()->profile()),
          browser()));
  controller->ClearProfileListForTesting();
  EXPECT_TRUE(controller->ShouldShow());
  dev_mode_extensions = controller->GetExtensionList();
  EXPECT_EQ(2U, dev_mode_extensions.size());
  bubble.set_controller(controller.get());
  bubble.Show();  // Simulate showing the bubble.
  EXPECT_EQ(1U, controller->link_click_count());
  EXPECT_EQ(0U, controller->action_click_count());
  EXPECT_EQ(0U, controller->dismiss_click_count());
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId1) != NULL);
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId2) != NULL);

  // Now disable the unpacked extension.
  service_->DisableExtension(kId1, Extension::DISABLE_USER_ACTION);
  service_->DisableExtension(kId2, Extension::DISABLE_USER_ACTION);

  controller.reset(
      new TestExtensionMessageBubbleController(
          new DevModeBubbleDelegate(browser()->profile()),
          browser()));
  controller->ClearProfileListForTesting();
  EXPECT_FALSE(controller->ShouldShow());
  dev_mode_extensions = controller->GetExtensionList();
  EXPECT_EQ(0U, dev_mode_extensions.size());
}

// The feature this is meant to test is only implemented on Windows.
#if defined(OS_WIN)
#define MAYBE_SettingsApiControllerTest SettingsApiControllerTest
#else
#define MAYBE_SettingsApiControllerTest DISABLED_SettingsApiControllerTest
#endif

TEST_F(ExtensionMessageBubbleTest, MAYBE_SettingsApiControllerTest) {
  Init();

  for (int i = 0; i < 3; ++i) {
    switch (static_cast<SettingsApiOverrideType>(i)) {
      case BUBBLE_TYPE_HOME_PAGE:
        // Load two extensions overriding home page and one overriding something
        // unrelated (to check for interference). Extension 2 should still win
        // on the home page setting.
        ASSERT_TRUE(LoadExtensionOverridingHome("1", kId1, Manifest::UNPACKED));
        ASSERT_TRUE(LoadExtensionOverridingHome("2", kId2, Manifest::UNPACKED));
        ASSERT_TRUE(
            LoadExtensionOverridingStart("3", kId3, Manifest::UNPACKED));
        break;
      case BUBBLE_TYPE_SEARCH_ENGINE:
        // We deliberately skip testing the search engine since it relies on
        // TemplateURLServiceFactory that isn't available while unit testing.
        // This test is only simulating the bubble interaction with the user and
        // that is more or less the same for the search engine as it is for the
        // others.
        continue;
      case BUBBLE_TYPE_STARTUP_PAGES:
        // Load two extensions overriding start page and one overriding
        // something unrelated (to check for interference). Extension 2 should
        // still win on the startup page setting.
        ASSERT_TRUE(
            LoadExtensionOverridingStart("1", kId1, Manifest::UNPACKED));
        ASSERT_TRUE(
            LoadExtensionOverridingStart("2", kId2, Manifest::UNPACKED));
        ASSERT_TRUE(LoadExtensionOverridingHome("3", kId3, Manifest::UNPACKED));
        break;
      default:
        NOTREACHED();
        break;
    }

    SettingsApiOverrideType type = static_cast<SettingsApiOverrideType>(i);
    std::unique_ptr<TestExtensionMessageBubbleController> controller(
        new TestExtensionMessageBubbleController(
            new SettingsApiBubbleDelegate(browser()->profile(), type),
            browser()));

    // The list will contain one enabled unpacked extension (ext 2).
    EXPECT_TRUE(controller->ShouldShow());
    std::vector<base::string16> override_extensions =
        controller->GetExtensionList();
    ASSERT_EQ(1U, override_extensions.size());
    EXPECT_TRUE(base::ASCIIToUTF16("Extension 2") ==
                override_extensions[0].c_str());
    EXPECT_EQ(0U, controller->link_click_count());
    EXPECT_EQ(0U, controller->dismiss_click_count());
    EXPECT_EQ(0U, controller->action_click_count());

    // Simulate showing the bubble and dismissing it.
    FakeExtensionMessageBubble bubble;
    bubble.set_action_on_show(
        FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_DISMISS_BUTTON);
    bubble.set_controller(controller.get());
    bubble.Show();
    EXPECT_EQ(0U, controller->link_click_count());
    EXPECT_EQ(0U, controller->action_click_count());
    EXPECT_EQ(1U, controller->dismiss_click_count());
    // No extension should have become disabled.
    ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
    EXPECT_TRUE(registry->enabled_extensions().GetByID(kId1) != NULL);
    EXPECT_TRUE(registry->enabled_extensions().GetByID(kId2) != NULL);
    EXPECT_TRUE(registry->enabled_extensions().GetByID(kId3) != NULL);
    // Only extension 2 should have been acknowledged.
    EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));
    EXPECT_TRUE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId2));
    EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId3));
    // Clean up after ourselves.
    controller->delegate()->SetBubbleInfoBeenAcknowledged(kId2, false);

    // Simulate clicking the learn more link to dismiss it.
    bubble.set_action_on_show(
        FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_LINK);
    controller.reset(
        new TestExtensionMessageBubbleController(
            new SettingsApiBubbleDelegate(browser()->profile(), type),
            browser()));
    bubble.set_controller(controller.get());
    bubble.Show();
    EXPECT_EQ(1U, controller->link_click_count());
    EXPECT_EQ(0U, controller->action_click_count());
    EXPECT_EQ(0U, controller->dismiss_click_count());
    // No extension should have become disabled.
    EXPECT_TRUE(registry->enabled_extensions().GetByID(kId1) != NULL);
    EXPECT_TRUE(registry->enabled_extensions().GetByID(kId2) != NULL);
    EXPECT_TRUE(registry->enabled_extensions().GetByID(kId3) != NULL);
    // Only extension 2 should have been acknowledged.
    EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));
    EXPECT_TRUE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId2));
    EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId3));
    // Clean up after ourselves.
    controller->delegate()->SetBubbleInfoBeenAcknowledged(kId2, false);

    // Do it again, but now opt to disable the extension.
    bubble.set_action_on_show(
        FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_ACTION_BUTTON);
    controller.reset(
        new TestExtensionMessageBubbleController(
            new SettingsApiBubbleDelegate(browser()->profile(), type),
            browser()));
    EXPECT_TRUE(controller->ShouldShow());
    override_extensions = controller->GetExtensionList();
    EXPECT_EQ(1U, override_extensions.size());
    bubble.set_controller(controller.get());
    bubble.Show();  // Simulate showing the bubble.
    EXPECT_EQ(0U, controller->link_click_count());
    EXPECT_EQ(1U, controller->action_click_count());
    EXPECT_EQ(0U, controller->dismiss_click_count());
    // Only extension 2 should have become disabled.
    EXPECT_TRUE(registry->enabled_extensions().GetByID(kId1) != NULL);
    EXPECT_TRUE(registry->disabled_extensions().GetByID(kId2) != NULL);
    EXPECT_TRUE(registry->enabled_extensions().GetByID(kId3) != NULL);
    // No extension should have been acknowledged (it got disabled).
    EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));
    EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId2));
    EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId3));

    // Clean up after ourselves.
    service_->UninstallExtension(kId1,
                                 extensions::UNINSTALL_REASON_FOR_TESTING,
                                 base::Bind(&base::DoNothing),
                                 NULL);
    service_->UninstallExtension(kId2,
                                 extensions::UNINSTALL_REASON_FOR_TESTING,
                                 base::Bind(&base::DoNothing),
                                 NULL);
    service_->UninstallExtension(kId3,
                                 extensions::UNINSTALL_REASON_FOR_TESTING,
                                 base::Bind(&base::DoNothing),
                                 NULL);
  }
}

// The feature this is meant to test is only enacted on Windows, but it should
// pass on all platforms.
TEST_F(ExtensionMessageBubbleTest, NtpOverriddenControllerTest) {
  Init();
  // Load two extensions overriding new tab page and one overriding something
  // unrelated (to check for interference). Extension 2 should still win
  // on the new tab page setting.
  ASSERT_TRUE(LoadExtensionOverridingNtp("1", kId1, Manifest::UNPACKED));
  ASSERT_TRUE(LoadExtensionOverridingNtp("2", kId2, Manifest::UNPACKED));
  ASSERT_TRUE(LoadExtensionOverridingStart("3", kId3, Manifest::UNPACKED));

  std::unique_ptr<TestExtensionMessageBubbleController> controller(
      new TestExtensionMessageBubbleController(
          new NtpOverriddenBubbleDelegate(browser()->profile()), browser()));

  // The list will contain one enabled unpacked extension (ext 2).
  EXPECT_TRUE(controller->ShouldShow());
  std::vector<base::string16> override_extensions =
      controller->GetExtensionList();
  ASSERT_EQ(1U, override_extensions.size());
  EXPECT_TRUE(base::ASCIIToUTF16("Extension 2") ==
              override_extensions[0].c_str());
  EXPECT_EQ(0U, controller->link_click_count());
  EXPECT_EQ(0U, controller->dismiss_click_count());
  EXPECT_EQ(0U, controller->action_click_count());

  // Simulate showing the bubble and dismissing it.
  FakeExtensionMessageBubble bubble;
  bubble.set_action_on_show(
      FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_DISMISS_BUTTON);
  EXPECT_TRUE(controller->ShouldShow());
  bubble.set_controller(controller.get());
  bubble.Show();
  EXPECT_EQ(0U, controller->link_click_count());
  EXPECT_EQ(0U, controller->action_click_count());
  EXPECT_EQ(1U, controller->dismiss_click_count());
  // No extension should have become disabled.
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId1) != NULL);
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId2) != NULL);
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId3) != NULL);
  // Only extension 2 should have been acknowledged.
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));
  EXPECT_TRUE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId2));
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId3));
  // Clean up after ourselves.
  controller->delegate()->SetBubbleInfoBeenAcknowledged(kId2, false);

  // Simulate clicking the learn more link to dismiss it.
  bubble.set_action_on_show(
      FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_LINK);
  controller.reset(
      new TestExtensionMessageBubbleController(
          new NtpOverriddenBubbleDelegate(browser()->profile()),
          browser()));
  EXPECT_TRUE(controller->ShouldShow());
  bubble.set_controller(controller.get());
  bubble.Show();
  EXPECT_EQ(1U, controller->link_click_count());
  EXPECT_EQ(0U, controller->action_click_count());
  EXPECT_EQ(0U, controller->dismiss_click_count());
  // No extension should have become disabled.
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId1) != NULL);
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId2) != NULL);
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId3) != NULL);
  // Only extension 2 should have been acknowledged.
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));
  EXPECT_TRUE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId2));
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId3));
  // Clean up after ourselves.
  controller->delegate()->SetBubbleInfoBeenAcknowledged(kId2, false);

  // Do it again, but now opt to disable the extension.
  bubble.set_action_on_show(
      FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_ACTION_BUTTON);
  controller.reset(
      new TestExtensionMessageBubbleController(
          new NtpOverriddenBubbleDelegate(browser()->profile()),
          browser()));
  EXPECT_TRUE(controller->ShouldShow());
  override_extensions = controller->GetExtensionList();
  EXPECT_EQ(1U, override_extensions.size());
  bubble.set_controller(controller.get());
  bubble.Show();  // Simulate showing the bubble.
  EXPECT_EQ(0U, controller->link_click_count());
  EXPECT_EQ(1U, controller->action_click_count());
  EXPECT_EQ(0U, controller->dismiss_click_count());
  // Only extension 2 should have become disabled.
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId1) != NULL);
  EXPECT_TRUE(registry->disabled_extensions().GetByID(kId2) != NULL);
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId3) != NULL);
  // No extension should have been acknowledged (it got disabled).
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId2));
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId3));

  // Clean up after ourselves.
  service_->UninstallExtension(kId1,
                               extensions::UNINSTALL_REASON_FOR_TESTING,
                               base::Bind(&base::DoNothing),
                               NULL);
  service_->UninstallExtension(kId2,
                               extensions::UNINSTALL_REASON_FOR_TESTING,
                               base::Bind(&base::DoNothing),
                               NULL);
  service_->UninstallExtension(kId3,
                               extensions::UNINSTALL_REASON_FOR_TESTING,
                               base::Bind(&base::DoNothing),
                               NULL);
}

void SetInstallTime(const std::string& extension_id,
                    const base::Time& time,
                    ExtensionPrefs* prefs) {
  std::string time_str = base::Int64ToString(time.ToInternalValue());
  prefs->UpdateExtensionPref(extension_id,
                             "install_time",
                             new base::StringValue(time_str));
}

// The feature this is meant to test is only implemented on Windows.
#if defined(OS_WIN)
// http://crbug.com/397426
#define MAYBE_ProxyOverriddenControllerTest DISABLED_ProxyOverriddenControllerTest
#else
#define MAYBE_ProxyOverriddenControllerTest DISABLED_ProxyOverriddenControllerTest
#endif

TEST_F(ExtensionMessageBubbleTest, MAYBE_ProxyOverriddenControllerTest) {
  Init();
  ExtensionPrefs* prefs = ExtensionPrefs::Get(profile());
  // Load two extensions overriding proxy and one overriding something
  // unrelated (to check for interference). Extension 2 should still win
  // on the proxy setting.
  ASSERT_TRUE(LoadExtensionOverridingProxy("1", kId1, Manifest::UNPACKED));
  ASSERT_TRUE(LoadExtensionOverridingProxy("2", kId2, Manifest::UNPACKED));
  ASSERT_TRUE(LoadExtensionOverridingStart("3", kId3, Manifest::UNPACKED));

  // The bubble will not show if the extension was installed in the last 7 days
  // so we artificially set the install time to simulate an old install during
  // testing.
  base::Time old_enough = base::Time::Now() - base::TimeDelta::FromDays(8);
  SetInstallTime(kId1, old_enough, prefs);
  SetInstallTime(kId2, base::Time::Now(), prefs);
  SetInstallTime(kId3, old_enough, prefs);

  std::unique_ptr<TestExtensionMessageBubbleController> controller(
      new TestExtensionMessageBubbleController(
          new ProxyOverriddenBubbleDelegate(browser()->profile()), browser()));

  // The second extension is too new to warn about.
  EXPECT_FALSE(controller->ShouldShow());
  EXPECT_FALSE(controller->ShouldShow());
  // Lets make it old enough.
  SetInstallTime(kId2, old_enough, prefs);

  // The list will contain one enabled unpacked extension (ext 2).
  EXPECT_TRUE(controller->ShouldShow());
  EXPECT_FALSE(controller->ShouldShow());
  std::vector<base::string16> override_extensions =
      controller->GetExtensionList();
  ASSERT_EQ(1U, override_extensions.size());
  EXPECT_EQ(base::ASCIIToUTF16("Extension 2"), override_extensions[0]);
  EXPECT_EQ(0U, controller->link_click_count());
  EXPECT_EQ(0U, controller->dismiss_click_count());
  EXPECT_EQ(0U, controller->action_click_count());

  // Simulate showing the bubble and dismissing it.
  FakeExtensionMessageBubble bubble;
  bubble.set_action_on_show(
      FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_DISMISS_BUTTON);
  bubble.set_controller(controller.get());
  bubble.Show();
  EXPECT_EQ(0U, controller->link_click_count());
  EXPECT_EQ(0U, controller->action_click_count());
  EXPECT_EQ(1U, controller->dismiss_click_count());
  // No extension should have become disabled.
  ExtensionRegistry* registry = ExtensionRegistry::Get(profile());
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId1) != NULL);
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId2) != NULL);
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId3) != NULL);
  // Only extension 2 should have been acknowledged.
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));
  EXPECT_TRUE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId2));
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId3));
  // Clean up after ourselves.
  controller->delegate()->SetBubbleInfoBeenAcknowledged(kId2, false);

  // Simulate clicking the learn more link to dismiss it.
  bubble.set_action_on_show(
      FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_LINK);
  controller.reset(
      new TestExtensionMessageBubbleController(
          new ProxyOverriddenBubbleDelegate(browser()->profile()),
          browser()));
  EXPECT_TRUE(controller->ShouldShow());
  bubble.set_controller(controller.get());
  bubble.Show();
  EXPECT_EQ(1U, controller->link_click_count());
  EXPECT_EQ(0U, controller->action_click_count());
  EXPECT_EQ(0U, controller->dismiss_click_count());
  // No extension should have become disabled.
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId1) != NULL);
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId2) != NULL);
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId3) != NULL);
  // Only extension 2 should have been acknowledged.
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));
  EXPECT_TRUE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId2));
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId3));
  // Clean up after ourselves.
  controller->delegate()->SetBubbleInfoBeenAcknowledged(kId2, false);

  // Do it again, but now opt to disable the extension.
  bubble.set_action_on_show(
      FakeExtensionMessageBubble::BUBBLE_ACTION_CLICK_ACTION_BUTTON);
  controller.reset(
      new TestExtensionMessageBubbleController(
          new ProxyOverriddenBubbleDelegate(browser()->profile()),
          browser()));
  EXPECT_TRUE(controller->ShouldShow());
  override_extensions = controller->GetExtensionList();
  EXPECT_EQ(1U, override_extensions.size());
  bubble.set_controller(controller.get());
  bubble.Show();  // Simulate showing the bubble.
  EXPECT_EQ(0U, controller->link_click_count());
  EXPECT_EQ(1U, controller->action_click_count());
  EXPECT_EQ(0U, controller->dismiss_click_count());
  // Only extension 2 should have become disabled.
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId1) != NULL);
  EXPECT_TRUE(registry->disabled_extensions().GetByID(kId2) != NULL);
  EXPECT_TRUE(registry->enabled_extensions().GetByID(kId3) != NULL);

  // No extension should have been acknowledged (it got disabled).
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId1));
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId2));
  EXPECT_FALSE(controller->delegate()->HasBubbleInfoBeenAcknowledged(kId3));

  // Clean up after ourselves.
  service_->UninstallExtension(kId1,
                               extensions::UNINSTALL_REASON_FOR_TESTING,
                               base::Bind(&base::DoNothing),
                               NULL);
  service_->UninstallExtension(kId2,
                               extensions::UNINSTALL_REASON_FOR_TESTING,
                               base::Bind(&base::DoNothing),
                               NULL);
  service_->UninstallExtension(kId3,
                               extensions::UNINSTALL_REASON_FOR_TESTING,
                               base::Bind(&base::DoNothing),
                               NULL);
}

// Tests that a bubble outliving the associated browser object doesn't crash.
// crbug.com/604003
TEST_F(ExtensionMessageBubbleTest, TestBubbleOutlivesBrowser) {
  FeatureSwitch::ScopedOverride force_dev_mode_highlighting(
      FeatureSwitch::force_dev_mode_highlighting(), true);
  Init();
  ToolbarActionsModelFactory::GetInstance()->SetTestingFactory(
      profile(), &BuildToolbarModel);
  ToolbarActionsModel* model = ToolbarActionsModel::Get(profile());
  base::RunLoop().RunUntilIdle();

  ASSERT_TRUE(LoadExtensionWithAction("1", kId1, Manifest::UNPACKED));

  std::unique_ptr<TestExtensionMessageBubbleController> controller(
      new TestExtensionMessageBubbleController(
          new DevModeBubbleDelegate(browser()->profile()), browser()));
  EXPECT_TRUE(controller->ShouldShow());
  EXPECT_EQ(1u, model->toolbar_items().size());
  controller->HighlightExtensionsIfNecessary();
  EXPECT_TRUE(ToolbarActionsModel::Get(profile())->is_highlighting());
  set_browser(nullptr);
  EXPECT_FALSE(ToolbarActionsModel::Get(profile())->is_highlighting());
  controller.reset();
}

}  // namespace extensions
