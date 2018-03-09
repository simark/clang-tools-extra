//===--- DraftStore.cpp - File contents container ---------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "DraftStore.h"
#include "SourceCode.h"
#include "llvm/Support/Errc.h"

using namespace clang;
using namespace clang::clangd;

llvm::Optional<std::string> DraftStore::getDraft(PathRef File) const {
  std::lock_guard<std::mutex> Lock(Mutex);

  auto It = Drafts.find(File);
  if (It == Drafts.end())
    return llvm::None;

  return It->second;
}

std::vector<Path> DraftStore::getActiveFiles() const {
  std::lock_guard<std::mutex> Lock(Mutex);
  std::vector<Path> ResultVector;

  for (auto DraftIt = Drafts.begin(); DraftIt != Drafts.end(); DraftIt++)
    ResultVector.push_back(DraftIt->getKey());

  return ResultVector;
}

void DraftStore::addDraft(PathRef File, StringRef Contents) {
  std::lock_guard<std::mutex> Lock(Mutex);

  Drafts[File] = Contents;
}

llvm::Expected<std::string> DraftStore::updateDraft(
    PathRef File, llvm::ArrayRef<TextDocumentContentChangeEvent> Changes) {
  std::lock_guard<std::mutex> Lock(Mutex);

  auto EntryIt = Drafts.find(File);
  if (EntryIt == Drafts.end()) {
    return llvm::make_error<llvm::StringError>(
        "Trying to do incremental update on non-added document: " + File,
        llvm::errc::invalid_argument);
  }

  std::string Contents = EntryIt->second;

  for (const TextDocumentContentChangeEvent &Change : Changes) {
    std::string NewContents;

    if (Change.range) {
      const Position &Start = Change.range->start;
      size_t StartIndex = positionToOffset(Contents, Start);
      if (StartIndex > Contents.length()) {
        return llvm::make_error<llvm::StringError>(
            llvm::formatv("Change's start position (line={0}, character={1}) is "
                          "out of range.",
                          Start.line, Start.character),
            llvm::errc::invalid_argument);
      }

      const Position &End = Change.range->end;
      size_t EndIndex = positionToOffset(Contents, End);
      if (EndIndex > Contents.length()) {
        return llvm::make_error<llvm::StringError>(
            llvm::formatv("Range's end position (line={0}, character={1}) is "
                          "out of range.",
                          End.line, End.character),
            llvm::errc::invalid_argument);
      }

      if (EndIndex < StartIndex) {
        return llvm::make_error<llvm::StringError>(
            llvm::formatv("Range's end position (line={0}, character={1}) is "
                          "before start position (line={2}, character={3}).",
                          End.line, End.character, Start.line, Start.character),
            llvm::errc::invalid_argument);
      }

      if (Change.rangeLength &&
          (ssize_t)(EndIndex - StartIndex) != *Change.rangeLength) {
        return llvm::make_error<llvm::StringError>(
            llvm::formatv("Change's rangeLength ({0}) doesn't match the "
                          "computed range length ({1}).",
                          *Change.rangeLength, EndIndex - StartIndex),
            llvm::errc::invalid_argument);
      }

      NewContents.reserve(Contents.length () - (EndIndex - StartIndex) + Change.text.length());

      NewContents = Contents.substr(0, StartIndex);
      NewContents += Change.text;

      if (EndIndex < Contents.length())
        NewContents += Contents.substr(EndIndex);
    } else {
      NewContents = Change.text;
    }

    Contents = std::move (NewContents);
  }

  EntryIt->second = Contents;
  return std::move (Contents);
}

void DraftStore::removeDraft(PathRef File) {
  std::lock_guard<std::mutex> Lock(Mutex);

  Drafts.erase(File);
}
