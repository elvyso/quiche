// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "spdy/core/hpack/hpack_entry.h"

#include "absl/hash/hash.h"
#include "common/platform/api/quiche_test.h"

namespace spdy {

namespace {

TEST(HpackLookupEntryTest, EntryNamesDiffer) {
  HpackLookupEntry entry1{"header", "value"};
  HpackLookupEntry entry2{"HEADER", "value"};

  EXPECT_FALSE(entry1 == entry2);
  EXPECT_NE(absl::Hash<HpackLookupEntry>()(entry1),
            absl::Hash<HpackLookupEntry>()(entry2));
}

TEST(HpackLookupEntryTest, EntryValuesDiffer) {
  HpackLookupEntry entry1{"header", "value"};
  HpackLookupEntry entry2{"header", "VALUE"};

  EXPECT_FALSE(entry1 == entry2);
  EXPECT_NE(absl::Hash<HpackLookupEntry>()(entry1),
            absl::Hash<HpackLookupEntry>()(entry2));
}

TEST(HpackLookupEntryTest, EntriesEqual) {
  HpackLookupEntry entry1{"name", "value"};
  HpackLookupEntry entry2{"name", "value"};

  EXPECT_TRUE(entry1 == entry2);
  EXPECT_EQ(absl::Hash<HpackLookupEntry>()(entry1),
            absl::Hash<HpackLookupEntry>()(entry2));
}

class HpackEntryTest : public QuicheTest {
 protected:
  HpackEntryTest()
      : name_("header-name"),
        value_("header value"),
        total_insertions_(0),
        table_size_(0) {}

  // These builders maintain the same external table invariants that a "real"
  // table (ie HpackHeaderTable) would.
  HpackEntry StaticEntry() {
    return HpackEntry(name_, value_, true, total_insertions_++);
  }
  HpackEntry DynamicEntry() {
    ++table_size_;
    size_t index = total_insertions_++;
    return HpackEntry(name_, value_, false, index);
  }

  // TODO(b/182789212): More meaningful tests of Size().
  size_t Size() {
    return name_.size() + value_.size() + kHpackEntrySizeOverhead;
  }

  std::string name_, value_;

 private:
  // Referenced by HpackEntry instances.
  size_t total_insertions_;
  size_t table_size_;
};

TEST_F(HpackEntryTest, StaticConstructor) {
  HpackEntry entry(StaticEntry());

  EXPECT_EQ(name_, entry.name());
  EXPECT_EQ(value_, entry.value());
  EXPECT_TRUE(entry.IsStatic());
  EXPECT_EQ(Size(), entry.Size());
}

TEST_F(HpackEntryTest, DynamicConstructor) {
  HpackEntry entry(DynamicEntry());

  EXPECT_EQ(name_, entry.name());
  EXPECT_EQ(value_, entry.value());
  EXPECT_FALSE(entry.IsStatic());
  EXPECT_EQ(Size(), entry.Size());
}

TEST_F(HpackEntryTest, DefaultConstructor) {
  HpackEntry entry;

  EXPECT_TRUE(entry.name().empty());
  EXPECT_TRUE(entry.value().empty());
  EXPECT_EQ(kHpackEntrySizeOverhead, entry.Size());
}

}  // namespace

}  // namespace spdy
