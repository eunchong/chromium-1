// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/installer/util/work_item_list.h"

#include <memory>

#include "base/macros.h"
#include "chrome/installer/util/work_item.h"
#include "chrome/installer/util/work_item_mocks.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using testing::InSequence;
using testing::Return;

// Execute a WorkItemList successfully and then rollback.
TEST(WorkItemListTest, ExecutionSuccess) {
  std::unique_ptr<WorkItemList> list(WorkItem::CreateWorkItemList());

  // Create the mock work items.
  std::unique_ptr<StrictMockWorkItem> item1(new StrictMockWorkItem);
  std::unique_ptr<StrictMockWorkItem> item2(new StrictMockWorkItem);
  std::unique_ptr<StrictMockWorkItem> item3(new StrictMockWorkItem);

  {
    // Expect all three items to be done in order then undone.
    InSequence s;

    EXPECT_CALL(*item1, Do()).WillOnce(Return(true));
    EXPECT_CALL(*item2, Do()).WillOnce(Return(true));
    EXPECT_CALL(*item3, Do()).WillOnce(Return(true));
    EXPECT_CALL(*item3, Rollback());
    EXPECT_CALL(*item2, Rollback());
    EXPECT_CALL(*item1, Rollback());
  }

  // Add the items to the list.
  list->AddWorkItem(item1.release());
  list->AddWorkItem(item2.release());
  list->AddWorkItem(item3.release());

  // Do and rollback the list.
  EXPECT_TRUE(list->Do());
  list->Rollback();
}

// Execute a WorkItemList. Fail in the middle. Rollback what has been done.
TEST(WorkItemListTest, ExecutionFailAndRollback) {
  std::unique_ptr<WorkItemList> list(WorkItem::CreateWorkItemList());

  // Create the mock work items.
  std::unique_ptr<StrictMockWorkItem> item1(new StrictMockWorkItem);
  std::unique_ptr<StrictMockWorkItem> item2(new StrictMockWorkItem);
  std::unique_ptr<StrictMockWorkItem> item3(new StrictMockWorkItem);

  {
    // Expect the two first work items to be done in order then undone.
    InSequence s;

    EXPECT_CALL(*item1, Do()).WillOnce(Return(true));
    EXPECT_CALL(*item2, Do()).WillOnce(Return(false));
    EXPECT_CALL(*item2, Rollback());
    EXPECT_CALL(*item1, Rollback());
  }

  // Add the items to the list.
  list->AddWorkItem(item1.release());
  list->AddWorkItem(item2.release());
  list->AddWorkItem(item3.release());

  // Do and rollback the list.
  EXPECT_FALSE(list->Do());
  list->Rollback();
}
