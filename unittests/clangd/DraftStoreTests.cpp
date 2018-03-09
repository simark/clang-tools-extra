//===-- DraftStoreTests.cpp - Clangd unit tests -----------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#include "Annotations.h"
#include "DraftStore.h"
#include "SourceCode.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace clang {
namespace clangd {
namespace {

using namespace llvm;

struct IncrementalTestStep {
  StringRef Src;
  StringRef Contents;
};

static int rangeLength(StringRef Code, const Range &Rng) {
  size_t Start = positionToOffset(Code, Rng.start);
  size_t End = positionToOffset(Code, Rng.end);

  assert(Start <= Code.size());
  assert(End <= Code.size());

  return End - Start;
}

// Send the changes one by one to updateDraft, verify the intermediate results.

static void stepByStep(llvm::ArrayRef<IncrementalTestStep> Steps) {
  DraftStore DS;
  Annotations InitialSrc(Steps.front().Src);
  const char Path[] = "/hello.cpp";

  // Set the initial content.
  DS.addDraft(Path, InitialSrc.code());

  for (size_t i = 1; i < Steps.size(); i++) {
    Annotations SrcBefore(Steps[i - 1].Src);
    Annotations SrcAfter(Steps[i].Src);
    StringRef Contents = Steps[i - 1].Contents;
    TextDocumentContentChangeEvent Event{
        SrcBefore.range(),
        rangeLength(SrcBefore.code(), SrcBefore.range()),
        Contents.str(),
    };

    llvm::Expected<std::string> Result = DS.updateDraft(Path, {Event});
    EXPECT_TRUE(!!Result);
    EXPECT_EQ(*Result, SrcAfter.code());
    EXPECT_EQ(*DS.getDraft(Path), SrcAfter.code());
  }
}

// Send all the changes at once to updateDraft, check only the final result.

static void allAtOnce(llvm::ArrayRef<IncrementalTestStep> Steps) {
  DraftStore DS;
  Annotations InitialSrc(Steps.front().Src);
  Annotations FinalSrc(Steps.back().Src);
  const char Path[] = "/hello.cpp";
  std::vector<TextDocumentContentChangeEvent> Changes;

  for (size_t i = 0; i < Steps.size() - 1; i++) {
    Annotations Src(Steps[i].Src);
    StringRef Contents = Steps[i].Contents;

    Changes.push_back({
        Src.range(),
        rangeLength(Src.code(), Src.range()),
        Contents.str(),
    });
  }

  // Set the initial content.
  DS.addDraft(Path, InitialSrc.code());

  llvm::Expected<std::string> Result = DS.updateDraft(Path, Changes);

  EXPECT_TRUE(!!Result) << llvm::toString(Result.takeError());
  EXPECT_EQ(*Result, FinalSrc.code());
  EXPECT_EQ(*DS.getDraft(Path), FinalSrc.code());
}

TEST(DraftStoreIncrementalUpdateTest, Simple) {
  // clang-format off
  IncrementalTestStep Steps[] =
    {
      // Replace a range
      {
R"cpp(static int
hello[[World]]()
{})cpp",
        "Universe"
      },
      // Delete a range
      {
R"cpp(static int
hello[[Universe]]()
{})cpp",
        ""
      },
      // Add a range
      {
R"cpp(static int
hello[[]]()
{})cpp",
        "Monde"
      },
      {
R"cpp(static int
helloMonde()
{})cpp",
        ""
      }
    };
  // clang-format on

  stepByStep(Steps);
  allAtOnce(Steps);
}

TEST(DraftStoreIncrementalUpdateTest, MultiLine) {
  // clang-format off
  IncrementalTestStep Steps[] =
    {
      // Replace a range
      {
R"cpp(static [[int
helloWorld]]()
{})cpp",
R"cpp(char
welcome)cpp"
      },
      // Delete a range
      {
R"cpp(static char[[
welcome]]()
{})cpp",
        ""
      },
      // Add a range
      {
R"cpp(static char[[]]()
{})cpp",
        R"cpp(
cookies)cpp"
      },
      // Replace the whole file
      {
R"cpp([[static char
cookies()
{}]])cpp",
        R"cpp(#include <stdio.h>
)cpp"
      },
      // Delete the whole file
      {
        R"cpp([[#include <stdio.h>
]])cpp",
        "",
      },
      // Add something to an empty file
      {
        "[[]]",
        R"cpp(int main() {
)cpp",
      },
      {
        R"cpp(int main() {
)cpp",
        ""
      }
    };
  // clang-format on

  stepByStep(Steps);
  allAtOnce(Steps);
}

TEST(DraftStoreIncrementalUpdateTest, WrongRangeLength) {
  DraftStore DS;
  Path File = "foo.cpp";

  DS.addDraft(File, "int main() {}\n");

  TextDocumentContentChangeEvent Change;
  Change.range.emplace();
  Change.range->start.line = 0;
  Change.range->start.character = 0;
  Change.range->end.line = 0;
  Change.range->end.character = 2;
  Change.rangeLength = 10;

  llvm::Expected<std::string> Result = DS.updateDraft(File, {Change});

  EXPECT_TRUE(!Result);
  EXPECT_EQ(
      llvm::toString(Result.takeError()),
      "Change's rangeLength (10) doesn't match the computed range length (2).");
}

TEST(DraftStoreIncrementalUpdateTest, EndBeforeStart) {
  DraftStore DS;
  Path File = "foo.cpp";

  DS.addDraft(File, "int main() {}\n");

  TextDocumentContentChangeEvent Change;
  Change.range.emplace();
  Change.range->start.line = 0;
  Change.range->start.character = 5;
  Change.range->end.line = 0;
  Change.range->end.character = 3;

  llvm::Expected<std::string> Result = DS.updateDraft(File, {Change});

  EXPECT_TRUE(!Result);
  EXPECT_EQ(llvm::toString(Result.takeError()),
            "Range's end position (line=0, character=3) is before start "
            "position (line=0, character=5).");
}

} // namespace
} // namespace clangd
} // namespace clang
