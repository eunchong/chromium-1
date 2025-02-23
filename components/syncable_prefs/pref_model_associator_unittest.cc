// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/syncable_prefs/pref_model_associator.h"

#include <memory>

#include "base/macros.h"
#include "base/memory/ref_counted.h"
#include "base/values.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/syncable_prefs/pref_model_associator_client.h"
#include "components/syncable_prefs/pref_service_mock_factory.h"
#include "components/syncable_prefs/pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace syncable_prefs {

namespace {

const char kStringPrefName[] = "pref.string";
const char kListPrefName[] = "pref.list";
const char kDictionaryPrefName[] = "pref.dictionary";

class TestPrefModelAssociatorClient : public PrefModelAssociatorClient {
 public:
  TestPrefModelAssociatorClient() {}
  ~TestPrefModelAssociatorClient() override {}

  // PrefModelAssociatorClient implementation.
  bool IsMergeableListPreference(const std::string& pref_name) const override {
    return pref_name == kListPrefName;
  }

  bool IsMergeableDictionaryPreference(
      const std::string& pref_name) const override {
    return pref_name == kDictionaryPrefName;
  }

 private:
  DISALLOW_COPY_AND_ASSIGN(TestPrefModelAssociatorClient);
};

class AbstractPreferenceMergeTest : public testing::Test {
 protected:
  AbstractPreferenceMergeTest() {
    PrefServiceMockFactory factory;
    factory.SetPrefModelAssociatorClient(&client_);
    scoped_refptr<user_prefs::PrefRegistrySyncable> pref_registry(
        new user_prefs::PrefRegistrySyncable);
    pref_registry->RegisterStringPref(
        kStringPrefName,
        std::string(),
        user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
    pref_registry->RegisterListPref(
        kListPrefName,
        user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
    pref_registry->RegisterDictionaryPref(
        kDictionaryPrefName,
        user_prefs::PrefRegistrySyncable::SYNCABLE_PREF);
    pref_service_ = factory.CreateSyncable(pref_registry.get());
    pref_sync_service_ = static_cast<PrefModelAssociator*>(
        pref_service_->GetSyncableService(syncer::PREFERENCES));
  }

  void SetContentPattern(base::DictionaryValue* patterns_dict,
                         const std::string& expression,
                         int setting) {
    base::DictionaryValue* expression_dict;
    bool found =
        patterns_dict->GetDictionaryWithoutPathExpansion(expression,
                                                         &expression_dict);
    if (!found) {
      expression_dict = new base::DictionaryValue;
      patterns_dict->SetWithoutPathExpansion(expression, expression_dict);
    }
    expression_dict->SetWithoutPathExpansion(
        "setting", new base::FundamentalValue(setting));
  }

  void SetPrefToEmpty(const std::string& pref_name) {
    std::unique_ptr<base::Value> empty_value;
    const PrefService::Preference* pref =
        pref_service_->FindPreference(pref_name.c_str());
    ASSERT_TRUE(pref);
    base::Value::Type type = pref->GetType();
    if (type == base::Value::TYPE_DICTIONARY)
      empty_value.reset(new base::DictionaryValue);
    else if (type == base::Value::TYPE_LIST)
      empty_value.reset(new base::ListValue);
    else
      FAIL();
    pref_service_->Set(pref_name.c_str(), *empty_value);
  }

  TestPrefModelAssociatorClient client_;
  std::unique_ptr<PrefServiceSyncable> pref_service_;
  PrefModelAssociator* pref_sync_service_;
};

class ListPreferenceMergeTest : public AbstractPreferenceMergeTest {
 protected:
  ListPreferenceMergeTest()
      : server_url0_("http://example.com/server0"),
        server_url1_("http://example.com/server1"),
        local_url0_("http://example.com/local0"),
        local_url1_("http://example.com/local1") {
    server_url_list_.Append(new base::StringValue(server_url0_));
    server_url_list_.Append(new base::StringValue(server_url1_));
  }

  std::string server_url0_;
  std::string server_url1_;
  std::string local_url0_;
  std::string local_url1_;
  base::ListValue server_url_list_;
};

TEST_F(ListPreferenceMergeTest, NotListOrDictionary) {
  pref_service_->SetString(kStringPrefName, local_url0_);
  const PrefService::Preference* pref =
      pref_service_->FindPreference(kStringPrefName);
  std::unique_ptr<base::Value> server_value(
      new base::StringValue(server_url0_));
  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      pref->name(), *pref->GetValue(), *server_value));
  EXPECT_TRUE(merged_value->Equals(server_value.get()));
}

TEST_F(ListPreferenceMergeTest, LocalEmpty) {
  SetPrefToEmpty(kListPrefName);
  const PrefService::Preference* pref =
      pref_service_->FindPreference(kListPrefName);
  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      pref->name(), *pref->GetValue(), server_url_list_));
  EXPECT_TRUE(merged_value->Equals(&server_url_list_));
}

TEST_F(ListPreferenceMergeTest, ServerNull) {
  std::unique_ptr<base::Value> null_value = base::Value::CreateNullValue();
  {
    ListPrefUpdate update(pref_service_.get(), kListPrefName);
    base::ListValue* local_list_value = update.Get();
    local_list_value->Append(new base::StringValue(local_url0_));
  }

  const PrefService::Preference* pref =
      pref_service_->FindPreference(kListPrefName);
  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      pref->name(), *pref->GetValue(), *null_value));
  const base::ListValue* local_list_value =
        pref_service_->GetList(kListPrefName);
  EXPECT_TRUE(merged_value->Equals(local_list_value));
}

TEST_F(ListPreferenceMergeTest, ServerEmpty) {
  std::unique_ptr<base::Value> empty_value(new base::ListValue);
  {
    ListPrefUpdate update(pref_service_.get(), kListPrefName);
    base::ListValue* local_list_value = update.Get();
    local_list_value->Append(new base::StringValue(local_url0_));
  }

  const PrefService::Preference* pref =
      pref_service_->FindPreference(kListPrefName);
  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      pref->name(), *pref->GetValue(), *empty_value));
  const base::ListValue* local_list_value =
        pref_service_->GetList(kListPrefName);
  EXPECT_TRUE(merged_value->Equals(local_list_value));
}

TEST_F(ListPreferenceMergeTest, Merge) {
  {
    ListPrefUpdate update(pref_service_.get(), kListPrefName);
    base::ListValue* local_list_value = update.Get();
    local_list_value->Append(new base::StringValue(local_url0_));
    local_list_value->Append(new base::StringValue(local_url1_));
  }

  const PrefService::Preference* pref =
      pref_service_->FindPreference(kListPrefName);
  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      pref->name(), *pref->GetValue(), server_url_list_));

  base::ListValue expected;
  expected.Append(new base::StringValue(server_url0_));
  expected.Append(new base::StringValue(server_url1_));
  expected.Append(new base::StringValue(local_url0_));
  expected.Append(new base::StringValue(local_url1_));
  EXPECT_TRUE(merged_value->Equals(&expected));
}

TEST_F(ListPreferenceMergeTest, Duplicates) {
  {
    ListPrefUpdate update(pref_service_.get(), kListPrefName);
    base::ListValue* local_list_value = update.Get();
    local_list_value->Append(new base::StringValue(local_url0_));
    local_list_value->Append(new base::StringValue(server_url0_));
    local_list_value->Append(new base::StringValue(server_url1_));
  }

  const PrefService::Preference* pref =
      pref_service_->FindPreference(kListPrefName);
  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      pref->name(), *pref->GetValue(), server_url_list_));

  base::ListValue expected;
  expected.Append(new base::StringValue(server_url0_));
  expected.Append(new base::StringValue(server_url1_));
  expected.Append(new base::StringValue(local_url0_));
  EXPECT_TRUE(merged_value->Equals(&expected));
}

TEST_F(ListPreferenceMergeTest, Equals) {
  {
    ListPrefUpdate update(pref_service_.get(), kListPrefName);
    base::ListValue* local_list_value = update.Get();
    local_list_value->Append(new base::StringValue(server_url0_));
    local_list_value->Append(new base::StringValue(server_url1_));
  }

  std::unique_ptr<base::Value> original(server_url_list_.DeepCopy());
  const PrefService::Preference* pref =
      pref_service_->FindPreference(kListPrefName);
  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      pref->name(), *pref->GetValue(), server_url_list_));
  EXPECT_TRUE(merged_value->Equals(original.get()));
}

class DictionaryPreferenceMergeTest : public AbstractPreferenceMergeTest {
 protected:
  DictionaryPreferenceMergeTest()
      : expression0_("expression0"),
        expression1_("expression1"),
        expression2_("expression2"),
        expression3_("expression3"),
        expression4_("expression4") {
    SetContentPattern(&server_patterns_, expression0_, 1);
    SetContentPattern(&server_patterns_, expression1_, 2);
    SetContentPattern(&server_patterns_, expression2_, 1);
  }

  std::string expression0_;
  std::string expression1_;
  std::string expression2_;
  std::string expression3_;
  std::string expression4_;
  base::DictionaryValue server_patterns_;
};

TEST_F(DictionaryPreferenceMergeTest, LocalEmpty) {
  SetPrefToEmpty(kDictionaryPrefName);
  const PrefService::Preference* pref =
      pref_service_->FindPreference(kDictionaryPrefName);
  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      pref->name(), *pref->GetValue(), server_patterns_));
  EXPECT_TRUE(merged_value->Equals(&server_patterns_));
}

TEST_F(DictionaryPreferenceMergeTest, ServerNull) {
  std::unique_ptr<base::Value> null_value = base::Value::CreateNullValue();
  {
    DictionaryPrefUpdate update(pref_service_.get(), kDictionaryPrefName);
    base::DictionaryValue* local_dict_value = update.Get();
    SetContentPattern(local_dict_value, expression3_, 1);
  }

  const PrefService::Preference* pref =
      pref_service_->FindPreference(kDictionaryPrefName);
  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      pref->name(), *pref->GetValue(), *null_value));
  const base::DictionaryValue* local_dict_value =
      pref_service_->GetDictionary(kDictionaryPrefName);
  EXPECT_TRUE(merged_value->Equals(local_dict_value));
}

TEST_F(DictionaryPreferenceMergeTest, ServerEmpty) {
  std::unique_ptr<base::Value> empty_value(new base::DictionaryValue);
  {
    DictionaryPrefUpdate update(pref_service_.get(), kDictionaryPrefName);
    base::DictionaryValue* local_dict_value = update.Get();
    SetContentPattern(local_dict_value, expression3_, 1);
  }

  const PrefService::Preference* pref =
      pref_service_->FindPreference(kDictionaryPrefName);
  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      pref->name(), *pref->GetValue(), *empty_value));
  const base::DictionaryValue* local_dict_value =
      pref_service_->GetDictionary(kDictionaryPrefName);
  EXPECT_TRUE(merged_value->Equals(local_dict_value));
}

TEST_F(DictionaryPreferenceMergeTest, MergeNoConflicts) {
  {
    DictionaryPrefUpdate update(pref_service_.get(), kDictionaryPrefName);
    base::DictionaryValue* local_dict_value = update.Get();
    SetContentPattern(local_dict_value, expression3_, 1);
  }

  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      kDictionaryPrefName,
      *pref_service_->FindPreference(kDictionaryPrefName)->GetValue(),
      server_patterns_));

  base::DictionaryValue expected;
  SetContentPattern(&expected, expression0_, 1);
  SetContentPattern(&expected, expression1_, 2);
  SetContentPattern(&expected, expression2_, 1);
  SetContentPattern(&expected, expression3_, 1);
  EXPECT_TRUE(merged_value->Equals(&expected));
}

TEST_F(DictionaryPreferenceMergeTest, MergeConflicts) {
  {
    DictionaryPrefUpdate update(pref_service_.get(), kDictionaryPrefName);
    base::DictionaryValue* local_dict_value = update.Get();
    SetContentPattern(local_dict_value, expression0_, 2);
    SetContentPattern(local_dict_value, expression2_, 1);
    SetContentPattern(local_dict_value, expression3_, 1);
    SetContentPattern(local_dict_value, expression4_, 2);
  }

  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      kDictionaryPrefName,
      *pref_service_->FindPreference(kDictionaryPrefName)->GetValue(),
      server_patterns_));

  base::DictionaryValue expected;
  SetContentPattern(&expected, expression0_, 1);
  SetContentPattern(&expected, expression1_, 2);
  SetContentPattern(&expected, expression2_, 1);
  SetContentPattern(&expected, expression3_, 1);
  SetContentPattern(&expected, expression4_, 2);
  EXPECT_TRUE(merged_value->Equals(&expected));
}

TEST_F(DictionaryPreferenceMergeTest, MergeValueToDictionary) {
  base::DictionaryValue local_dict_value;
  local_dict_value.SetInteger("key", 0);

  base::DictionaryValue server_dict_value;
  server_dict_value.SetInteger("key.subkey", 0);

  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      kDictionaryPrefName, local_dict_value, server_dict_value));

  EXPECT_TRUE(merged_value->Equals(&server_dict_value));
}

TEST_F(DictionaryPreferenceMergeTest, Equal) {
  {
    DictionaryPrefUpdate update(pref_service_.get(), kDictionaryPrefName);
    base::DictionaryValue* local_dict_value = update.Get();
    SetContentPattern(local_dict_value, expression0_, 1);
    SetContentPattern(local_dict_value, expression1_, 2);
    SetContentPattern(local_dict_value, expression2_, 1);
  }

  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      kDictionaryPrefName,
      *pref_service_->FindPreference(kDictionaryPrefName)->GetValue(),
      server_patterns_));
  EXPECT_TRUE(merged_value->Equals(&server_patterns_));
}

TEST_F(DictionaryPreferenceMergeTest, ConflictButServerWins) {
  {
    DictionaryPrefUpdate update(pref_service_.get(), kDictionaryPrefName);
    base::DictionaryValue* local_dict_value = update.Get();
    SetContentPattern(local_dict_value, expression0_, 2);
    SetContentPattern(local_dict_value, expression1_, 2);
    SetContentPattern(local_dict_value, expression2_, 1);
  }

  std::unique_ptr<base::Value> merged_value(pref_sync_service_->MergePreference(
      kDictionaryPrefName,
      *pref_service_->FindPreference(kDictionaryPrefName)->GetValue(),
      server_patterns_));
  EXPECT_TRUE(merged_value->Equals(&server_patterns_));
}

class IndividualPreferenceMergeTest : public AbstractPreferenceMergeTest {
 protected:
  IndividualPreferenceMergeTest()
      : url0_("http://example.com/server0"),
        url1_("http://example.com/server1"),
        expression0_("expression0"),
        expression1_("expression1") {
    server_url_list_.Append(new base::StringValue(url0_));
    SetContentPattern(&server_patterns_, expression0_, 1);
  }

  bool MergeListPreference(const char* pref) {
    {
      ListPrefUpdate update(pref_service_.get(), pref);
      base::ListValue* local_list_value = update.Get();
      local_list_value->Append(new base::StringValue(url1_));
    }

    std::unique_ptr<base::Value> merged_value(
        pref_sync_service_->MergePreference(
            pref, *pref_service_->GetUserPrefValue(pref), server_url_list_));

    base::ListValue expected;
    expected.Append(new base::StringValue(url0_));
    expected.Append(new base::StringValue(url1_));
    return merged_value->Equals(&expected);
  }

  bool MergeDictionaryPreference(const char* pref) {
    {
      DictionaryPrefUpdate update(pref_service_.get(), pref);
      base::DictionaryValue* local_dict_value = update.Get();
      SetContentPattern(local_dict_value, expression1_, 1);
    }

    std::unique_ptr<base::Value> merged_value(
        pref_sync_service_->MergePreference(
            pref, *pref_service_->GetUserPrefValue(pref), server_patterns_));

    base::DictionaryValue expected;
    SetContentPattern(&expected, expression0_, 1);
    SetContentPattern(&expected, expression1_, 1);
    return merged_value->Equals(&expected);
  }

  std::string url0_;
  std::string url1_;
  std::string expression0_;
  std::string expression1_;
  std::string content_type0_;
  base::ListValue server_url_list_;
  base::DictionaryValue server_patterns_;
};

TEST_F(IndividualPreferenceMergeTest, ListPreference) {
  EXPECT_TRUE(MergeListPreference(kListPrefName));
}

}  // namespace

}  // namespace syncable_prefs
