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

#include <folly/io/async/ScopedEventBaseThread.h>

#include <folly/Function.h>
#include <folly/Range.h>
#include <folly/io/async/EventBaseManager.h>
#include <folly/system/ThreadName.h>

using namespace std;

#if PROFILING_ENABLED
namespace {
std::unordered_map<std::string, uint64_t> totElapsed;
}
#endif
namespace folly {

static void run(
    EventBaseManager* ebm,
    EventBase* eb,
    folly::Baton<>* stop,
    const StringPiece& name) {
  uint64_t st = microtime();
  if (!name.empty()) {
    folly::setThreadName(name);
  }

  ebm->setEventBase(eb, false);
#if PROFILING_ENABLED
  totElapsed["run-1"] += microtime() - st;
  VLOG_EVERY_N(1, 100) << "folly::run() PART 1"
                       << " tot = " << totElapsed["run-1"] << " micros"
                       << (totElapsed["run-1"] = 0);
#endif
  eb->loopForever();
#if PROFILING_ENABLED
  st = microtime();
#endif
  // must destruct in io thread for on-destruction callbacks
  eb->runOnDestruction([=] { ebm->clearEventBase(); });
  // wait until terminateLoopSoon() is complete
  stop->wait(folly::Baton<>::wait_options().logging_enabled(false));
  eb->~EventBase();
#if PROFILING_ENABLED
  totElapsed["run-2"] += microtime() - st;
  VLOG_EVERY_N(1, 100) << "folly::run() PART 2"
                       << " tot = " << totElapsed["run-2"] << " micros"
                       << (totElapsed["run-2"] = 0);
#endif
}

ScopedEventBaseThread::ScopedEventBaseThread()
    : ScopedEventBaseThread(nullptr, "") {}

ScopedEventBaseThread::ScopedEventBaseThread(StringPiece name)
    : ScopedEventBaseThread(nullptr, name) {}

ScopedEventBaseThread::ScopedEventBaseThread(EventBaseManager* ebm)
    : ScopedEventBaseThread(ebm, "") {}

ScopedEventBaseThread::ScopedEventBaseThread(
    EventBaseManager* ebm, StringPiece name)
    : ScopedEventBaseThread(EventBase::Options(), ebm, name) {}

ScopedEventBaseThread::ScopedEventBaseThread(
    EventBase::Options eventBaseOptions,
    EventBaseManager* ebm,
    StringPiece name)
    : ebm_(ebm ? ebm : EventBaseManager::get()) {
  new (&eb_) EventBase(std::move(eventBaseOptions));
  th_ = rt::Thread(std::bind(run, ebm_, &eb_, &stop_, name));
  eb_.waitUntilRunning();
}

ScopedEventBaseThread::~ScopedEventBaseThread() {
  eb_.terminateLoopSoon();
  stop_.post();
  th_.Join();
}

} // namespace folly
