// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef QUICHE_SPDY_CORE_HPACK_HPACK_ENTRY_H_
#define QUICHE_SPDY_CORE_HPACK_HPACK_ENTRY_H_

#include <cstddef>
#include <cstdint>
#include <string>

#include "absl/strings/string_view.h"
#include "common/platform/api/quiche_export.h"

// All section references below are to
// http://tools.ietf.org/html/draft-ietf-httpbis-header-compression-08

namespace spdy {

// The constant amount added to name().size() and value().size() to
// get the size of an HpackEntry as defined in 5.1.
constexpr size_t kHpackEntrySizeOverhead = 32;

// A structure for looking up entries in the static and dynamic tables.
struct QUICHE_EXPORT_PRIVATE HpackLookupEntry {
  absl::string_view name;
  absl::string_view value;

  bool operator==(const HpackLookupEntry& other) const {
    return name == other.name && value == other.value;
  }

  // Absail hashing framework extension according to absl/hash/hash.h:
  template <typename H>
  friend H AbslHashValue(H h, const HpackLookupEntry& entry) {
    return H::combine(std::move(h), entry.name, entry.value);
  }
};

// A structure for an entry in the static table (3.3.1)
// and the header table (3.3.2).
class QUICHE_EXPORT_PRIVATE HpackEntry {
 public:
  // Creates an entry. Preconditions:
  // - |is_static| captures whether this entry is a member of the static
  //   or dynamic header table.
  // - |insertion_index| is this entry's index in the total set of entries ever
  //   inserted into the header table (including static entries).
  //
  // The combination of |is_static| and |insertion_index| allows an
  // HpackEntryTable to determine the index of an HpackEntry in O(1) time.
  // Copies |name| and |value|.
  HpackEntry(absl::string_view name,
             absl::string_view value,
             bool is_static,
             size_t insertion_index);

  HpackEntry(const HpackEntry& other);
  HpackEntry& operator=(const HpackEntry& other);

  // Creates an entry with empty name and value. Only defined so that
  // entries can be stored in STL containers.
  HpackEntry();

  ~HpackEntry();

  absl::string_view name() const { return name_ref_; }
  absl::string_view value() const { return value_ref_; }

  // Returns whether this entry is a member of the static (as opposed to
  // dynamic) table.
  // TODO(b/182789212): Remove.
  bool IsStatic() const { return type_ == STATIC; }

  // Returns whether this entry is a lookup-only entry.
  // TODO(b/182789212): Remove.
  bool IsLookup() const { return type_ == LOOKUP; }

  // Used to compute the entry's index in the header table.
  size_t InsertionIndex() const { return insertion_index_; }

  // Returns the size of an entry as defined in 5.1.
  static size_t Size(absl::string_view name, absl::string_view value);
  size_t Size() const;

  std::string GetDebugString() const;

  // Returns the estimate of dynamically allocated memory in bytes.
  size_t EstimateMemoryUsage() const;

 private:
  // TODO(b/182789212): Remove.
  enum EntryType {
    LOOKUP,
    DYNAMIC,
    STATIC,
  };

  // These members are not used for LOOKUP entries.
  std::string name_;
  std::string value_;

  // These members are always valid. For DYNAMIC and STATIC entries, they
  // always point to |name_| and |value_|.
  // TODO(b/182789212): Remove.
  absl::string_view name_ref_;
  absl::string_view value_ref_;

  // The entry's index in the total set of entries ever inserted into the header
  // table.
  size_t insertion_index_;

  // TODO(b/182789212): Remove.
  EntryType type_;
};

}  // namespace spdy

#endif  // QUICHE_SPDY_CORE_HPACK_HPACK_ENTRY_H_
