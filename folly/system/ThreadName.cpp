/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <folly/system/ThreadName.h>

#include <type_traits>

#include <folly/Portability.h>
#include <folly/Traits.h>

namespace folly {

bool canSetCurrentThreadName() {
  return false;
}

bool canSetOtherThreadName() {
  return false;
}

static constexpr size_t kMaxThreadNameLength = 16;

static Optional<std::string> getPThreadName(pthread_t pid) {
  return none;
}

Optional<std::string> getThreadName(rt::Thread::Id id) {
  return none;
}

Optional<std::string> getCurrentThreadName() {
  return getThreadName(rt::GetId());
}

bool setThreadName(rt::Thread::Id tid, StringPiece name) {
  return false;
}

bool setThreadName(pthread_t pid, StringPiece name) {
  (void)pid;
  return false;
}

bool setThreadName(StringPiece name) {
  return setThreadName(rt::GetId(), name);
}
} // namespace folly
