// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/variations/variations_seed_processor.h"

#include <stddef.h>
#include <stdint.h>

#include <map>
#include <memory>
#include <utility>
#include <vector>

#include "base/bind.h"
#include "base/command_line.h"
#include "base/feature_list.h"
#include "base/format_macros.h"
#include "base/macros.h"
#include "base/strings/string_split.h"
#include "base/strings/stringprintf.h"
#include "base/strings/utf_string_conversions.h"
#include "components/variations/processed_study.h"
#include "components/variations/variations_associated_data.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace variations {

namespace {

// Converts |time| to Study proto format.
int64_t TimeToProtoTime(const base::Time& time) {
  return (time - base::Time::UnixEpoch()).InSeconds();
}

// Constants for testing associating command line flags with trial groups.
const char kFlagStudyName[] = "flag_test_trial";
const char kFlagGroup1Name[] = "flag_group1";
const char kFlagGroup2Name[] = "flag_group2";
const char kNonFlagGroupName[] = "non_flag_group";
const char kForcingFlag1[] = "flag_test1";
const char kForcingFlag2[] = "flag_test2";

const VariationID kExperimentId = 123;

// Adds an experiment to |study| with the specified |name| and |probability|.
Study_Experiment* AddExperiment(const std::string& name, int probability,
                                Study* study) {
  Study_Experiment* experiment = study->add_experiment();
  experiment->set_name(name);
  experiment->set_probability_weight(probability);
  return experiment;
}

// Populates |study| with test data used for testing associating command line
// flags with trials groups. The study will contain three groups, a default
// group that isn't associated with a flag, and two other groups, both
// associated with different flags.
Study CreateStudyWithFlagGroups(int default_group_probability,
                                int flag_group1_probability,
                                int flag_group2_probability) {
  DCHECK_GE(default_group_probability, 0);
  DCHECK_GE(flag_group1_probability, 0);
  DCHECK_GE(flag_group2_probability, 0);
  Study study;
  study.set_name(kFlagStudyName);
  study.set_default_experiment_name(kNonFlagGroupName);

  AddExperiment(kNonFlagGroupName, default_group_probability, &study);
  AddExperiment(kFlagGroup1Name, flag_group1_probability, &study)
      ->set_forcing_flag(kForcingFlag1);
  AddExperiment(kFlagGroup2Name, flag_group2_probability, &study)
      ->set_forcing_flag(kForcingFlag2);

  return study;
}

class TestOverrideStringCallback {
 public:
  typedef std::map<uint32_t, base::string16> OverrideMap;

  TestOverrideStringCallback()
      : callback_(base::Bind(&TestOverrideStringCallback::Override,
                             base::Unretained(this))) {}

  virtual ~TestOverrideStringCallback() {}

  const VariationsSeedProcessor::UIStringOverrideCallback& callback() const {
    return callback_;
  }

  const OverrideMap& overrides() const { return overrides_; }

 private:
  void Override(uint32_t hash, const base::string16& string) {
    overrides_[hash] = string;
  }

  VariationsSeedProcessor::UIStringOverrideCallback callback_;
  OverrideMap overrides_;

  DISALLOW_COPY_AND_ASSIGN(TestOverrideStringCallback);
};

}  // namespace

class VariationsSeedProcessorTest : public ::testing::Test {
 public:
  VariationsSeedProcessorTest() {
  }

  ~VariationsSeedProcessorTest() override {
    // Ensure that the maps are cleared between tests, since they are stored as
    // process singletons.
    testing::ClearAllVariationIDs();
    testing::ClearAllVariationParams();

    base::FeatureList::ClearInstanceForTesting();
  }

  bool CreateTrialFromStudy(const Study* study) {
    return CreateTrialFromStudyWithFeatureList(study, &feature_list_);
  }

  bool CreateTrialFromStudyWithFeatureList(const Study* study,
                                           base::FeatureList* feature_list) {
    ProcessedStudy processed_study;
    if (processed_study.Init(study, false)) {
      VariationsSeedProcessor().CreateTrialFromStudy(
          processed_study, override_callback_.callback(), feature_list);
      return true;
    }
    return false;
  }

 protected:
  base::FeatureList feature_list_;
  TestOverrideStringCallback override_callback_;

 private:
  DISALLOW_COPY_AND_ASSIGN(VariationsSeedProcessorTest);
};

TEST_F(VariationsSeedProcessorTest, AllowForceGroupAndVariationId) {
  base::CommandLine::ForCurrentProcess()->AppendSwitch(kForcingFlag1);

  base::FieldTrialList field_trial_list(NULL);

  Study study = CreateStudyWithFlagGroups(100, 0, 0);
  study.mutable_experiment(1)->set_google_web_experiment_id(kExperimentId);

  EXPECT_TRUE(CreateTrialFromStudy(&study));
  EXPECT_EQ(kFlagGroup1Name,
            base::FieldTrialList::FindFullName(kFlagStudyName));

  VariationID id = GetGoogleVariationID(GOOGLE_WEB_PROPERTIES, kFlagStudyName,
                                        kFlagGroup1Name);
  EXPECT_EQ(kExperimentId, id);
}

// Test that the group for kForcingFlag1 is forced.
TEST_F(VariationsSeedProcessorTest, ForceGroupWithFlag1) {
  base::CommandLine::ForCurrentProcess()->AppendSwitch(kForcingFlag1);

  base::FieldTrialList field_trial_list(NULL);

  Study study = CreateStudyWithFlagGroups(100, 0, 0);
  EXPECT_TRUE(CreateTrialFromStudy(&study));
  EXPECT_EQ(kFlagGroup1Name,
            base::FieldTrialList::FindFullName(kFlagStudyName));
}

// Test that the group for kForcingFlag2 is forced.
TEST_F(VariationsSeedProcessorTest, ForceGroupWithFlag2) {
  base::CommandLine::ForCurrentProcess()->AppendSwitch(kForcingFlag2);

  base::FieldTrialList field_trial_list(NULL);

  Study study = CreateStudyWithFlagGroups(100, 0, 0);
  EXPECT_TRUE(CreateTrialFromStudy(&study));
  EXPECT_EQ(kFlagGroup2Name,
            base::FieldTrialList::FindFullName(kFlagStudyName));
}

TEST_F(VariationsSeedProcessorTest, ForceGroup_ChooseFirstGroupWithFlag) {
  // Add the flag to the command line arguments so the flag group is forced.
  base::CommandLine::ForCurrentProcess()->AppendSwitch(kForcingFlag1);
  base::CommandLine::ForCurrentProcess()->AppendSwitch(kForcingFlag2);

  base::FieldTrialList field_trial_list(NULL);

  Study study = CreateStudyWithFlagGroups(100, 0, 0);
  EXPECT_TRUE(CreateTrialFromStudy(&study));
  EXPECT_EQ(kFlagGroup1Name,
            base::FieldTrialList::FindFullName(kFlagStudyName));
}

TEST_F(VariationsSeedProcessorTest, ForceGroup_DontChooseGroupWithFlag) {
  base::FieldTrialList field_trial_list(NULL);

  // The two flag groups are given high probability, which would normally make
  // them very likely to be chosen. They won't be chosen since flag groups are
  // never chosen when their flag isn't present.
  Study study = CreateStudyWithFlagGroups(1, 999, 999);
  EXPECT_TRUE(CreateTrialFromStudy(&study));
  EXPECT_EQ(kNonFlagGroupName,
            base::FieldTrialList::FindFullName(kFlagStudyName));
}

TEST_F(VariationsSeedProcessorTest,
       NonExpiredStudyPrioritizedOverExpiredStudy) {
  VariationsSeedProcessor seed_processor;

  const std::string kTrialName = "A";
  const std::string kGroup1Name = "Group1";

  VariationsSeed seed;
  Study* study1 = seed.add_study();
  study1->set_name(kTrialName);
  study1->set_default_experiment_name("Default");
  AddExperiment(kGroup1Name, 100, study1);
  AddExperiment("Default", 0, study1);
  Study* study2 = seed.add_study();
  *study2 = *study1;
  ASSERT_EQ(seed.study(0).name(), seed.study(1).name());

  const base::Time year_ago =
      base::Time::Now() - base::TimeDelta::FromDays(365);

  const base::Version version("20.0.0.0");

  // Check that adding [expired, non-expired] activates the non-expired one.
  ASSERT_EQ(std::string(), base::FieldTrialList::FindFullName(kTrialName));
  {
    base::FeatureList feature_list;
    base::FieldTrialList field_trial_list(NULL);
    study1->set_expiry_date(TimeToProtoTime(year_ago));
    seed_processor.CreateTrialsFromSeed(
        seed, "en-CA", base::Time::Now(), version, Study_Channel_STABLE,
        Study_FormFactor_DESKTOP, "", "", "", override_callback_.callback(),
        &feature_list);
    EXPECT_EQ(kGroup1Name, base::FieldTrialList::FindFullName(kTrialName));
  }

  // Check that adding [non-expired, expired] activates the non-expired one.
  ASSERT_EQ(std::string(), base::FieldTrialList::FindFullName(kTrialName));
  {
    base::FeatureList feature_list;
    base::FieldTrialList field_trial_list(NULL);
    study1->clear_expiry_date();
    study2->set_expiry_date(TimeToProtoTime(year_ago));
    seed_processor.CreateTrialsFromSeed(
        seed, "en-CA", base::Time::Now(), version, Study_Channel_STABLE,
        Study_FormFactor_DESKTOP, "", "", "", override_callback_.callback(),
        &feature_list);
    EXPECT_EQ(kGroup1Name, base::FieldTrialList::FindFullName(kTrialName));
  }
}

TEST_F(VariationsSeedProcessorTest, OverrideUIStrings) {
  base::FieldTrialList field_trial_list(NULL);

  Study study;
  study.set_name("Study1");
  study.set_default_experiment_name("B");
  study.set_activation_type(Study_ActivationType_ACTIVATION_AUTO);

  Study_Experiment* experiment1 = AddExperiment("A", 0, &study);
  Study_Experiment_OverrideUIString* override =
      experiment1->add_override_ui_string();

  override->set_name_hash(1234);
  override->set_value("test");

  Study_Experiment* experiment2 = AddExperiment("B", 1, &study);

  EXPECT_TRUE(CreateTrialFromStudy(&study));

  const TestOverrideStringCallback::OverrideMap& overrides =
      override_callback_.overrides();

  EXPECT_TRUE(overrides.empty());

  study.set_name("Study2");
  experiment1->set_probability_weight(1);
  experiment2->set_probability_weight(0);

  EXPECT_TRUE(CreateTrialFromStudy(&study));

  EXPECT_EQ(1u, overrides.size());
  TestOverrideStringCallback::OverrideMap::const_iterator it =
      overrides.find(1234);
  EXPECT_EQ(base::ASCIIToUTF16("test"), it->second);
}

TEST_F(VariationsSeedProcessorTest, OverrideUIStringsWithForcingFlag) {
  Study study = CreateStudyWithFlagGroups(100, 0, 0);
  ASSERT_EQ(kForcingFlag1, study.experiment(1).forcing_flag());

  study.set_activation_type(Study_ActivationType_ACTIVATION_AUTO);
  Study_Experiment_OverrideUIString* override =
      study.mutable_experiment(1)->add_override_ui_string();
  override->set_name_hash(1234);
  override->set_value("test");

  base::CommandLine::ForCurrentProcess()->AppendSwitch(kForcingFlag1);
  base::FieldTrialList field_trial_list(NULL);
  EXPECT_TRUE(CreateTrialFromStudy(&study));
  EXPECT_EQ(kFlagGroup1Name, base::FieldTrialList::FindFullName(study.name()));

  const TestOverrideStringCallback::OverrideMap& overrides =
      override_callback_.overrides();
  EXPECT_EQ(1u, overrides.size());
  TestOverrideStringCallback::OverrideMap::const_iterator it =
      overrides.find(1234);
  EXPECT_EQ(base::ASCIIToUTF16("test"), it->second);
}

TEST_F(VariationsSeedProcessorTest, ValidateStudy) {
  Study study;
  study.set_default_experiment_name("def");
  AddExperiment("abc", 100, &study);
  Study_Experiment* default_group = AddExperiment("def", 200, &study);

  ProcessedStudy processed_study;
  EXPECT_TRUE(processed_study.Init(&study, false));
  EXPECT_EQ(300, processed_study.total_probability());
  EXPECT_FALSE(processed_study.all_assignments_to_one_group());

  // Min version checks.
  study.mutable_filter()->set_min_version("1.2.3.*");
  EXPECT_TRUE(processed_study.Init(&study, false));
  study.mutable_filter()->set_min_version("1.*.3");
  EXPECT_FALSE(processed_study.Init(&study, false));
  study.mutable_filter()->set_min_version("1.2.3");
  EXPECT_TRUE(processed_study.Init(&study, false));

  // Max version checks.
  study.mutable_filter()->set_max_version("2.3.4.*");
  EXPECT_TRUE(processed_study.Init(&study, false));
  study.mutable_filter()->set_max_version("*.3");
  EXPECT_FALSE(processed_study.Init(&study, false));
  study.mutable_filter()->set_max_version("2.3.4");
  EXPECT_TRUE(processed_study.Init(&study, false));

  study.clear_default_experiment_name();
  EXPECT_FALSE(processed_study.Init(&study, false));

  study.set_default_experiment_name("xyz");
  EXPECT_FALSE(processed_study.Init(&study, false));

  study.set_default_experiment_name("def");
  default_group->clear_name();
  EXPECT_FALSE(processed_study.Init(&study, false));

  default_group->set_name("def");
  EXPECT_TRUE(processed_study.Init(&study, false));
  Study_Experiment* repeated_group = study.add_experiment();
  repeated_group->set_name("abc");
  repeated_group->set_probability_weight(1);
  EXPECT_FALSE(processed_study.Init(&study, false));
}

TEST_F(VariationsSeedProcessorTest, ValidateStudySingleFeature) {
  Study study;
  study.set_default_experiment_name("def");
  Study_Experiment* exp1 = AddExperiment("exp1", 100, &study);
  Study_Experiment* exp2 = AddExperiment("exp2", 100, &study);
  Study_Experiment* exp3 = AddExperiment("exp3", 100, &study);
  AddExperiment("def", 100, &study);

  ProcessedStudy processed_study;
  EXPECT_TRUE(processed_study.Init(&study, false));
  EXPECT_EQ(400, processed_study.total_probability());

  EXPECT_EQ(std::string(), processed_study.single_feature_name());

  const char kFeature1Name[] = "Feature1";
  const char kFeature2Name[] = "Feature2";

  exp1->mutable_feature_association()->add_enable_feature(kFeature1Name);
  EXPECT_TRUE(processed_study.Init(&study, false));
  EXPECT_EQ(kFeature1Name, processed_study.single_feature_name());

  exp1->clear_feature_association();
  exp1->mutable_feature_association()->add_enable_feature(kFeature1Name);
  exp1->mutable_feature_association()->add_enable_feature(kFeature2Name);
  EXPECT_TRUE(processed_study.Init(&study, false));
  // Since there's multiple different features, |single_feature_name| should be
  // unset.
  EXPECT_EQ(std::string(), processed_study.single_feature_name());

  exp1->clear_feature_association();
  exp1->mutable_feature_association()->add_enable_feature(kFeature1Name);
  exp2->mutable_feature_association()->add_enable_feature(kFeature1Name);
  exp3->mutable_feature_association()->add_disable_feature(kFeature1Name);
  EXPECT_TRUE(processed_study.Init(&study, false));
  EXPECT_EQ(kFeature1Name, processed_study.single_feature_name());

  // Setting a different feature name on exp2 should cause |single_feature_name|
  // to be not set.
  exp2->mutable_feature_association()->set_enable_feature(0, kFeature2Name);
  EXPECT_TRUE(processed_study.Init(&study, false));
  EXPECT_EQ(std::string(), processed_study.single_feature_name());
}

TEST_F(VariationsSeedProcessorTest, ProcessedStudyAllAssignmentsToOneGroup) {
  Study study;
  study.set_default_experiment_name("def");
  AddExperiment("def", 100, &study);

  ProcessedStudy processed_study;
  EXPECT_TRUE(processed_study.Init(&study, false));
  EXPECT_TRUE(processed_study.all_assignments_to_one_group());

  AddExperiment("abc", 0, &study);
  AddExperiment("flag", 0, &study)->set_forcing_flag(kForcingFlag1);
  EXPECT_TRUE(processed_study.Init(&study, false));
  EXPECT_TRUE(processed_study.all_assignments_to_one_group());

  AddExperiment("xyz", 1, &study);
  EXPECT_TRUE(processed_study.Init(&study, false));
  EXPECT_FALSE(processed_study.all_assignments_to_one_group());

  // Try with default group and first group being at 0.
  Study study2;
  study2.set_default_experiment_name("def");
  AddExperiment("def", 0, &study2);
  AddExperiment("xyz", 34, &study2);
  EXPECT_TRUE(processed_study.Init(&study2, false));
  EXPECT_TRUE(processed_study.all_assignments_to_one_group());
  AddExperiment("abc", 12, &study2);
  EXPECT_TRUE(processed_study.Init(&study2, false));
  EXPECT_FALSE(processed_study.all_assignments_to_one_group());
}

TEST_F(VariationsSeedProcessorTest, VariationParams) {
  base::FieldTrialList field_trial_list(NULL);

  Study study;
  study.set_name("Study1");
  study.set_default_experiment_name("B");

  Study_Experiment* experiment1 = AddExperiment("A", 1, &study);
  Study_Experiment_Param* param = experiment1->add_param();
  param->set_name("x");
  param->set_value("y");

  Study_Experiment* experiment2 = AddExperiment("B", 0, &study);

  EXPECT_TRUE(CreateTrialFromStudy(&study));
  EXPECT_EQ("y", GetVariationParamValue("Study1", "x"));

  study.set_name("Study2");
  experiment1->set_probability_weight(0);
  experiment2->set_probability_weight(1);
  EXPECT_TRUE(CreateTrialFromStudy(&study));
  EXPECT_EQ(std::string(), GetVariationParamValue("Study2", "x"));
}

TEST_F(VariationsSeedProcessorTest, VariationParamsWithForcingFlag) {
  Study study = CreateStudyWithFlagGroups(100, 0, 0);
  ASSERT_EQ(kForcingFlag1, study.experiment(1).forcing_flag());
  Study_Experiment_Param* param = study.mutable_experiment(1)->add_param();
  param->set_name("x");
  param->set_value("y");

  base::CommandLine::ForCurrentProcess()->AppendSwitch(kForcingFlag1);
  base::FieldTrialList field_trial_list(NULL);
  EXPECT_TRUE(CreateTrialFromStudy(&study));
  EXPECT_EQ(kFlagGroup1Name, base::FieldTrialList::FindFullName(study.name()));
  EXPECT_EQ("y", GetVariationParamValue(study.name(), "x"));
}

TEST_F(VariationsSeedProcessorTest, StartsActive) {
  base::FieldTrialList field_trial_list(NULL);

  VariationsSeed seed;
  Study* study1 = seed.add_study();
  study1->set_name("A");
  study1->set_default_experiment_name("Default");
  AddExperiment("AA", 100, study1);
  AddExperiment("Default", 0, study1);

  Study* study2 = seed.add_study();
  study2->set_name("B");
  study2->set_default_experiment_name("Default");
  AddExperiment("BB", 100, study2);
  AddExperiment("Default", 0, study2);
  study2->set_activation_type(Study_ActivationType_ACTIVATION_AUTO);

  Study* study3 = seed.add_study();
  study3->set_name("C");
  study3->set_default_experiment_name("Default");
  AddExperiment("CC", 100, study3);
  AddExperiment("Default", 0, study3);
  study3->set_activation_type(Study_ActivationType_ACTIVATION_EXPLICIT);

  VariationsSeedProcessor seed_processor;
  seed_processor.CreateTrialsFromSeed(
      seed, "en-CA", base::Time::Now(), base::Version("20.0.0.0"),
      Study_Channel_STABLE, Study_FormFactor_DESKTOP, "", "", "",
      override_callback_.callback(), &feature_list_);

  // Non-specified and ACTIVATION_EXPLICIT should not start active, but
  // ACTIVATION_AUTO should.
  EXPECT_FALSE(base::FieldTrialList::IsTrialActive("A"));
  EXPECT_TRUE(base::FieldTrialList::IsTrialActive("B"));
  EXPECT_FALSE(base::FieldTrialList::IsTrialActive("C"));

  EXPECT_EQ("AA", base::FieldTrialList::FindFullName("A"));
  EXPECT_EQ("BB", base::FieldTrialList::FindFullName("B"));
  EXPECT_EQ("CC", base::FieldTrialList::FindFullName("C"));

  // Now, all studies should be active.
  EXPECT_TRUE(base::FieldTrialList::IsTrialActive("A"));
  EXPECT_TRUE(base::FieldTrialList::IsTrialActive("B"));
  EXPECT_TRUE(base::FieldTrialList::IsTrialActive("C"));
}

TEST_F(VariationsSeedProcessorTest, StartsActiveWithFlag) {
  base::CommandLine::ForCurrentProcess()->AppendSwitch(kForcingFlag1);

  base::FieldTrialList field_trial_list(NULL);

  Study study = CreateStudyWithFlagGroups(100, 0, 0);
  study.set_activation_type(Study_ActivationType_ACTIVATION_AUTO);

  EXPECT_TRUE(CreateTrialFromStudy(&study));
  EXPECT_TRUE(base::FieldTrialList::IsTrialActive(kFlagStudyName));

  EXPECT_EQ(kFlagGroup1Name,
            base::FieldTrialList::FindFullName(kFlagStudyName));
}

TEST_F(VariationsSeedProcessorTest, ForcingFlagAlreadyForced) {
  Study study = CreateStudyWithFlagGroups(100, 0, 0);
  ASSERT_EQ(kNonFlagGroupName, study.experiment(0).name());
  Study_Experiment_Param* param = study.mutable_experiment(0)->add_param();
  param->set_name("x");
  param->set_value("y");
  study.mutable_experiment(0)->set_google_web_experiment_id(kExperimentId);

  base::FieldTrialList field_trial_list(NULL);
  base::FieldTrialList::CreateFieldTrial(kFlagStudyName, kNonFlagGroupName);

  base::CommandLine::ForCurrentProcess()->AppendSwitch(kForcingFlag1);
  EXPECT_TRUE(CreateTrialFromStudy(&study));
  // The previously forced experiment should still hold.
  EXPECT_EQ(kNonFlagGroupName,
            base::FieldTrialList::FindFullName(study.name()));

  // Check that params and experiment ids correspond.
  EXPECT_EQ("y", GetVariationParamValue(study.name(), "x"));
  VariationID id = GetGoogleVariationID(GOOGLE_WEB_PROPERTIES, kFlagStudyName,
                                        kNonFlagGroupName);
  EXPECT_EQ(kExperimentId, id);
}

TEST_F(VariationsSeedProcessorTest, FeatureEnabledOrDisableByTrial) {
  struct base::Feature kFeatureOffByDefault {
    "kOff", base::FEATURE_DISABLED_BY_DEFAULT
  };
  struct base::Feature kFeatureOnByDefault {
    "kOn", base::FEATURE_ENABLED_BY_DEFAULT
  };
  struct base::Feature kUnrelatedFeature {
    "kUnrelated", base::FEATURE_DISABLED_BY_DEFAULT
  };

  struct {
    const char* enable_feature;
    const char* disable_feature;
    bool expected_feature_off_state;
    bool expected_feature_on_state;
  } test_cases[] = {
      {nullptr, nullptr, false, true},
      {kFeatureOnByDefault.name, nullptr, false, true},
      {kFeatureOffByDefault.name, nullptr, true, true},
      {nullptr, kFeatureOnByDefault.name, false, false},
      {nullptr, kFeatureOffByDefault.name, false, true},
  };

  for (size_t i = 0; i < arraysize(test_cases); i++) {
    const auto& test_case = test_cases[i];
    SCOPED_TRACE(base::StringPrintf("Test[%" PRIuS "]", i));

    base::FieldTrialList field_trial_list(NULL);
    base::FeatureList::ClearInstanceForTesting();
    std::unique_ptr<base::FeatureList> feature_list(new base::FeatureList);

    Study study;
    study.set_name("Study1");
    study.set_default_experiment_name("B");
    AddExperiment("B", 0, &study);

    Study_Experiment* experiment = AddExperiment("A", 1, &study);
    Study_Experiment_FeatureAssociation* association =
        experiment->mutable_feature_association();
    if (test_case.enable_feature)
      association->add_enable_feature(test_case.enable_feature);
    else if (test_case.disable_feature)
      association->add_disable_feature(test_case.disable_feature);

    EXPECT_TRUE(
        CreateTrialFromStudyWithFeatureList(&study, feature_list.get()));
    base::FeatureList::SetInstance(std::move(feature_list));

    // |kUnrelatedFeature| should not be affected.
    EXPECT_FALSE(base::FeatureList::IsEnabled(kUnrelatedFeature));

    // Before the associated feature is queried, the trial shouldn't be active.
    EXPECT_FALSE(base::FieldTrialList::IsTrialActive(study.name()));

    EXPECT_EQ(test_case.expected_feature_off_state,
              base::FeatureList::IsEnabled(kFeatureOffByDefault));
    EXPECT_EQ(test_case.expected_feature_on_state,
              base::FeatureList::IsEnabled(kFeatureOnByDefault));

    // The field trial should get activated if it had a feature association.
    const bool expected_field_trial_active =
        test_case.enable_feature || test_case.disable_feature;
    EXPECT_EQ(expected_field_trial_active,
              base::FieldTrialList::IsTrialActive(study.name()));
  }
}

TEST_F(VariationsSeedProcessorTest, FeatureAssociationAndForcing) {
  struct base::Feature kFeatureOffByDefault {
    "kFeatureOffByDefault", base::FEATURE_DISABLED_BY_DEFAULT
  };
  struct base::Feature kFeatureOnByDefault {
    "kFeatureOnByDefault", base::FEATURE_ENABLED_BY_DEFAULT
  };

  enum OneHundredPercentGroup {
    DEFAULT_GROUP,
    ENABLE_GROUP,
    DISABLE_GROUP,
  };

  const char kDefaultGroup[] = "Default";
  const char kEnabledGroup[] = "Enabled";
  const char kDisabledGroup[] = "Disabled";
  const char kForcedOnGroup[] = "ForcedOn";
  const char kForcedOffGroup[] = "ForcedOff";

  struct {
    const base::Feature& feature;
    const char* enable_features_command_line;
    const char* disable_features_command_line;
    OneHundredPercentGroup one_hundred_percent_group;

    const char* expected_group;
    bool expected_feature_state;
    bool expected_trial_activated;
  } test_cases[] = {
      // Check what happens without and command-line forcing flags - that the
      // |one_hundred_percent_group| gets correctly selected and does the right
      // thing w.r.t. to affecting the feature / activating the trial.
      {kFeatureOffByDefault, "", "", DEFAULT_GROUP, kDefaultGroup, false, true},
      {kFeatureOffByDefault, "", "", ENABLE_GROUP, kEnabledGroup, true, true},
      {kFeatureOffByDefault, "", "", DISABLE_GROUP, kDisabledGroup, false,
       true},

      // Do the same as above, but for kFeatureOnByDefault feature.
      {kFeatureOnByDefault, "", "", DEFAULT_GROUP, kDefaultGroup, true, true},
      {kFeatureOnByDefault, "", "", ENABLE_GROUP, kEnabledGroup, true, true},
      {kFeatureOnByDefault, "", "", DISABLE_GROUP, kDisabledGroup, false, true},

      // Test forcing each feature on and off through the command-line and that
      // the correct associated experiment gets chosen.
      {kFeatureOffByDefault, kFeatureOffByDefault.name, "", DEFAULT_GROUP,
       kForcedOnGroup, true, true},
      {kFeatureOffByDefault, "", kFeatureOffByDefault.name, DEFAULT_GROUP,
       kForcedOffGroup, false, true},
      {kFeatureOnByDefault, kFeatureOnByDefault.name, "", DEFAULT_GROUP,
       kForcedOnGroup, true, true},
      {kFeatureOnByDefault, "", kFeatureOnByDefault.name, DEFAULT_GROUP,
       kForcedOffGroup, false, true},

      // Check that even if a feature should be enabled or disabled based on the
      // the experiment probability weights, the forcing flag association still
      // takes precedence. This is 4 cases as above, but with different values
      // for |one_hundred_percent_group|.
      {kFeatureOffByDefault, kFeatureOffByDefault.name, "", ENABLE_GROUP,
       kForcedOnGroup, true, true},
      {kFeatureOffByDefault, "", kFeatureOffByDefault.name, ENABLE_GROUP,
       kForcedOffGroup, false, true},
      {kFeatureOnByDefault, kFeatureOnByDefault.name, "", ENABLE_GROUP,
       kForcedOnGroup, true, true},
      {kFeatureOnByDefault, "", kFeatureOnByDefault.name, ENABLE_GROUP,
       kForcedOffGroup, false, true},
      {kFeatureOffByDefault, kFeatureOffByDefault.name, "", DISABLE_GROUP,
       kForcedOnGroup, true, true},
      {kFeatureOffByDefault, "", kFeatureOffByDefault.name, DISABLE_GROUP,
       kForcedOffGroup, false, true},
      {kFeatureOnByDefault, kFeatureOnByDefault.name, "", DISABLE_GROUP,
       kForcedOnGroup, true, true},
      {kFeatureOnByDefault, "", kFeatureOnByDefault.name, DISABLE_GROUP,
       kForcedOffGroup, false, true},
  };

  for (size_t i = 0; i < arraysize(test_cases); i++) {
    const auto& test_case = test_cases[i];
    const int group = test_case.one_hundred_percent_group;
    SCOPED_TRACE(base::StringPrintf(
        "Test[%" PRIuS "]: %s [%s] [%s] %d", i, test_case.feature.name,
        test_case.enable_features_command_line,
        test_case.disable_features_command_line, static_cast<int>(group)));

    base::FieldTrialList field_trial_list(NULL);
    base::FeatureList::ClearInstanceForTesting();
    std::unique_ptr<base::FeatureList> feature_list(new base::FeatureList);
    feature_list->InitializeFromCommandLine(
        test_case.enable_features_command_line,
        test_case.disable_features_command_line);

    Study study;
    study.set_name("Study1");
    study.set_default_experiment_name("Default");
    AddExperiment(kDefaultGroup, group == DEFAULT_GROUP ? 1 : 0, &study);

    Study_Experiment* feature_enable =
        AddExperiment(kEnabledGroup, group == ENABLE_GROUP ? 1 : 0, &study);
    feature_enable->mutable_feature_association()->add_enable_feature(
        test_case.feature.name);

    Study_Experiment* feature_disable =
        AddExperiment(kDisabledGroup, group == DISABLE_GROUP ? 1 : 0, &study);
    feature_disable->mutable_feature_association()->add_disable_feature(
        test_case.feature.name);

    AddExperiment(kForcedOnGroup, 0, &study)
        ->mutable_feature_association()
        ->set_forcing_feature_on(test_case.feature.name);
    AddExperiment(kForcedOffGroup, 0, &study)
        ->mutable_feature_association()
        ->set_forcing_feature_off(test_case.feature.name);

    EXPECT_TRUE(
        CreateTrialFromStudyWithFeatureList(&study, feature_list.get()));
    base::FeatureList::SetInstance(std::move(feature_list));

    // Trial should not be activated initially, but later might get activated
    // depending on the expected values.
    EXPECT_FALSE(base::FieldTrialList::IsTrialActive(study.name()));
    EXPECT_EQ(test_case.expected_feature_state,
              base::FeatureList::IsEnabled(test_case.feature));
    EXPECT_EQ(test_case.expected_trial_activated,
              base::FieldTrialList::IsTrialActive(study.name()));
  }
}

}  // namespace variations
