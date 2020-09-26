// Copyright 2020 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_METADATA_H_
#define SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_METADATA_H_

#include <lib/fit/result.h>

#include <optional>
#include <string>

#include "src/developer/forensics/feedback_data/annotations/types.h"
#include "src/developer/forensics/feedback_data/attachments/types.h"

namespace forensics {
namespace feedback_data {

// Constructs metadata describing the rest of the content of the snapshot archive.
class Metadata {
 public:
  Metadata(const AnnotationKeys& annotation_allowlist, const AttachmentKeys& attachment_allowlist);

  // Return a JSON metadata string.
  //
  // |missing_non_platform_annotations| indicates whether some non-platform annotations are
  // missing, i.e. whether clients tried to insert more non-platform annotations than the maximum
  // number of non-platform annotations the Datastore can hold.
  std::string MakeMetadata(const ::fit::result<Annotations>& annotations,
                           const ::fit::result<Attachments>& attachments,
                           bool missing_non_platform_annotations) const;

  static constexpr const char* kVersion = "1";

 private:
  AnnotationKeys annotation_allowlist_;
  AttachmentKeys attachment_allowlist_;
};

}  // namespace feedback_data
}  // namespace forensics

#endif  // SRC_DEVELOPER_FORENSICS_FEEDBACK_DATA_METADATA_H_