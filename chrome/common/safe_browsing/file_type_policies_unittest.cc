// Copyright 2016 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/common/safe_browsing/file_type_policies.h"

#include <string.h>

#include <memory>

#include "base/files/file_path.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::NiceMock;

namespace safe_browsing {

class MockFileTypePolicies : public FileTypePolicies {
 public:
  MockFileTypePolicies() {}
  virtual ~MockFileTypePolicies() {}

  MOCK_METHOD2(RecordUpdateMetrics, void(UpdateResult, const std::string&));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockFileTypePolicies);
};

class FileTypePoliciesTest : public testing::Test {
 protected:
  FileTypePoliciesTest() {}
  ~FileTypePoliciesTest() {}

 protected:
  NiceMock<MockFileTypePolicies> policies_;
};

TEST_F(FileTypePoliciesTest, UnpackResourceBundle) {
  EXPECT_CALL(policies_,
              RecordUpdateMetrics(FileTypePolicies::UpdateResult::SUCCESS,
                                  "ResourceBundle"));
  policies_.PopulateFromResourceBundle();

  // Look up a few well known types to ensure they're present.
  // Some types vary by OS, and we check one per OS to validate
  // that gen_file_type_proto.py does its job.
  //
  // NOTE: If the settings for these change in download_file_types.asciipb,
  // then you'll need to change them here as well.

  // Lookup .exe that varies on OS_WIN.
  base::FilePath exe_file(FILE_PATH_LITERAL("a/foo.exe"));
  DownloadFileType file_type = policies_.PolicyForFile(exe_file);
  EXPECT_EQ("exe", file_type.extension());
  EXPECT_EQ(0l, file_type.uma_value());
  EXPECT_EQ(false, file_type.is_archive());
  EXPECT_EQ(DownloadFileType::FULL_PING, file_type.ping_setting());
#if defined(OS_WIN)
  EXPECT_EQ(DownloadFileType::ALLOW_ON_USER_GESTURE,
            file_type.platform_settings(0).danger_level());
  EXPECT_EQ(DownloadFileType::DISALLOW_AUTO_OPEN,
            file_type.platform_settings(0).auto_open_hint());
#else
  EXPECT_EQ(DownloadFileType::NOT_DANGEROUS,
            file_type.platform_settings(0).danger_level());
  EXPECT_EQ(DownloadFileType::ALLOW_AUTO_OPEN,
            file_type.platform_settings(0).auto_open_hint());
#endif

  // Lookup .class that varies on OS_CHROMEOS, and also has a
  // default setting set.
  base::FilePath class_file(FILE_PATH_LITERAL("foo.class"));
  file_type = policies_.PolicyForFile(class_file);
  EXPECT_EQ("class", file_type.extension());
  EXPECT_EQ(13l, file_type.uma_value());
  EXPECT_EQ(false, file_type.is_archive());
  EXPECT_EQ(DownloadFileType::FULL_PING, file_type.ping_setting());
#if defined(OS_CHROMEOS)
  EXPECT_EQ(DownloadFileType::NOT_DANGEROUS,
            file_type.platform_settings(0).danger_level());
  EXPECT_EQ(DownloadFileType::ALLOW_AUTO_OPEN,
            file_type.platform_settings(0).auto_open_hint());
#else
  EXPECT_EQ(DownloadFileType::DANGEROUS,
            file_type.platform_settings(0).danger_level());
  EXPECT_EQ(DownloadFileType::DISALLOW_AUTO_OPEN,
            file_type.platform_settings(0).auto_open_hint());
#endif

  // Lookup .dmg that varies on OS_MACOS
  base::FilePath dmg_file(FILE_PATH_LITERAL("foo.dmg"));
  file_type = policies_.PolicyForFile(dmg_file);
  EXPECT_EQ("dmg", file_type.extension());
  EXPECT_EQ(21, file_type.uma_value());
  EXPECT_EQ(false, file_type.is_archive());
  EXPECT_EQ(DownloadFileType::FULL_PING, file_type.ping_setting());
#if defined(OS_MACOSX)
  EXPECT_EQ(DownloadFileType::ALLOW_ON_USER_GESTURE,
            file_type.platform_settings(0).danger_level());
  EXPECT_EQ(DownloadFileType::DISALLOW_AUTO_OPEN,
            file_type.platform_settings(0).auto_open_hint());
#else
  EXPECT_EQ(DownloadFileType::NOT_DANGEROUS,
            file_type.platform_settings(0).danger_level());
  EXPECT_EQ(DownloadFileType::ALLOW_AUTO_OPEN,
            file_type.platform_settings(0).auto_open_hint());
#endif

  // Lookup .dex that varies on OS_ANDROID
  base::FilePath dex_file(FILE_PATH_LITERAL("foo.dex"));
  file_type = policies_.PolicyForFile(dex_file);
  EXPECT_EQ("dex", file_type.extension());
  EXPECT_EQ(143, file_type.uma_value());
  EXPECT_EQ(false, file_type.is_archive());
  EXPECT_EQ(DownloadFileType::FULL_PING, file_type.ping_setting());
#if defined(OS_ANDROID)
  EXPECT_EQ(DownloadFileType::ALLOW_ON_USER_GESTURE,
            file_type.platform_settings(0).danger_level());
  EXPECT_EQ(DownloadFileType::DISALLOW_AUTO_OPEN,
            file_type.platform_settings(0).auto_open_hint());
#else
  EXPECT_EQ(DownloadFileType::NOT_DANGEROUS,
            file_type.platform_settings(0).danger_level());
  EXPECT_EQ(DownloadFileType::ALLOW_AUTO_OPEN,
            file_type.platform_settings(0).auto_open_hint());
#endif

  // Lookup .rpm that varies on OS_LINUX
  base::FilePath rpm_file(FILE_PATH_LITERAL("foo.rpm"));
  file_type = policies_.PolicyForFile(rpm_file);
  EXPECT_EQ("rpm", file_type.extension());
  EXPECT_EQ(142, file_type.uma_value());
  EXPECT_EQ(false, file_type.is_archive());
  EXPECT_EQ(DownloadFileType::FULL_PING, file_type.ping_setting());
#if defined(OS_LINUX) && !defined(OS_CHROMEOS)
  EXPECT_EQ(DownloadFileType::ALLOW_ON_USER_GESTURE,
            file_type.platform_settings(0).danger_level());
  EXPECT_EQ(DownloadFileType::DISALLOW_AUTO_OPEN,
            file_type.platform_settings(0).auto_open_hint());
#else
  EXPECT_EQ(DownloadFileType::NOT_DANGEROUS,
            file_type.platform_settings(0).danger_level());
  EXPECT_EQ(DownloadFileType::ALLOW_AUTO_OPEN,
            file_type.platform_settings(0).auto_open_hint());
#endif

  // Look .zip, an archive.  The same on all platforms.
  base::FilePath zip_file(FILE_PATH_LITERAL("b/bar.txt.zip"));
  file_type = policies_.PolicyForFile(zip_file);
  EXPECT_EQ("zip", file_type.extension());
  EXPECT_EQ(7l, file_type.uma_value());
  EXPECT_EQ(true, file_type.is_archive());
  EXPECT_EQ(DownloadFileType::FULL_PING, file_type.ping_setting());
  EXPECT_EQ(DownloadFileType::NOT_DANGEROUS,
            file_type.platform_settings(0).danger_level());

  // Check other accessors
  EXPECT_EQ(7l, policies_.UmaValueForFile(zip_file));
  EXPECT_EQ(true, policies_.IsFileAnArchive(zip_file));

  // Verify settings on the default type.
  file_type = policies_.PolicyForFile(
      base::FilePath(FILE_PATH_LITERAL("a/foo.fooobar")));
  EXPECT_EQ("", file_type.extension());
  EXPECT_EQ(18l, file_type.uma_value());
  EXPECT_EQ(false, file_type.is_archive());
  EXPECT_EQ(DownloadFileType::SAMPLED_PING, file_type.ping_setting());
  EXPECT_EQ(DownloadFileType::NOT_DANGEROUS,
            file_type.platform_settings(0).danger_level());
  EXPECT_EQ(DownloadFileType::ALLOW_AUTO_OPEN,
            file_type.platform_settings(0).auto_open_hint());
}

TEST_F(FileTypePoliciesTest, BadProto) {
  EXPECT_EQ(FileTypePolicies::UpdateResult::FAILED_EMPTY,
            policies_.PopulateFromBinaryPb(std::string()));

  EXPECT_EQ(FileTypePolicies::UpdateResult::FAILED_PROTO_PARSE,
            policies_.PopulateFromBinaryPb("foobar"));

  DownloadFileTypeConfig cfg;
  cfg.set_sampled_ping_probability(0.1f);
  EXPECT_EQ(FileTypePolicies::UpdateResult::FAILED_DEFAULT_SETTING_SET,
            policies_.PopulateFromBinaryPb(cfg.SerializeAsString()));

  cfg.mutable_default_file_type()->add_platform_settings();
  auto file_type = cfg.add_file_types();  // This is missing a platform_setting.
  EXPECT_EQ(FileTypePolicies::UpdateResult::FAILED_WRONG_SETTINGS_COUNT,
            policies_.PopulateFromBinaryPb(cfg.SerializeAsString()));

  file_type->add_platform_settings();
  EXPECT_EQ(FileTypePolicies::UpdateResult::SUCCESS,
            policies_.PopulateFromBinaryPb(cfg.SerializeAsString()));
}

TEST_F(FileTypePoliciesTest, BadUpdateFromExisting) {
  // Make a minimum viable config
  DownloadFileTypeConfig cfg;
  cfg.mutable_default_file_type()->add_platform_settings();
  cfg.add_file_types()->add_platform_settings();
  cfg.set_version_id(2);
  EXPECT_EQ(FileTypePolicies::UpdateResult::SUCCESS,
            policies_.PopulateFromBinaryPb(cfg.SerializeAsString()));

  // Can't update to the same version
  EXPECT_EQ(FileTypePolicies::UpdateResult::FAILED_VERSION_CHECK,
            policies_.PopulateFromBinaryPb(cfg.SerializeAsString()));

  // Can't go backward
  cfg.set_version_id(1);
  EXPECT_EQ(FileTypePolicies::UpdateResult::FAILED_VERSION_CHECK,
            policies_.PopulateFromBinaryPb(cfg.SerializeAsString()));
}
}  // namespace safe_browsing
