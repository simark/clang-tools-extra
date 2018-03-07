//===--- Logger.cpp - Logger interface for clangd -------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Logger.h"
#include "llvm/Support/raw_ostream.h"
#include <mutex>

namespace clang {
namespace clangd {

namespace {
Logger *L = nullptr;
bool Verbose_ = false;

} // namespace

LoggingSession::LoggingSession(clangd::Logger &Instance, bool Verbose) {
  assert(!L);
  L = &Instance;
  Verbose_ = Verbose;
}

LoggingSession::~LoggingSession() { L = nullptr; }

void log(const llvm::Twine &Message) {
  if (L)
    L->log(Message);
  else {
    static std::mutex Mu;
    std::lock_guard<std::mutex> Guard(Mu);
    llvm::errs() << Message << "\n";
  }
}

void vlog(const llvm::Twine &Message) {
  if (L)
    L->vlog(Message);
  else
    log(Message);
}

} // namespace clangd
} // namespace clang
