// stem_conflict_test.cpp — unit checks for findOutputStemConflicts()
#include "config/FilenameConfig.hpp"
#include "TestAudioUtils.hpp"

#include <cstdio>
#include <vector>

int main(int argc, char* argv[])
{
    // Interface uniformity with the rest of the suite: accept the scratch
    // directory argument (see TestAudioUtils.hpp). This test is pure logic and
    // generates no files.
    TestAudioUtils::scratchDirFromArgs(argc, argv, "manifold_stem_conflict_test");

    int failures = 0;
    const auto check = [&failures](bool ok, const char* what) {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", what);
        if (!ok) ++failures;
    };

    check(findOutputStemConflicts({}).empty(), "empty input -> no conflicts");
    check(findOutputStemConflicts({"a", "b", "c"}).empty(), "unique stems -> no conflicts");

    const auto c1 = findOutputStemConflicts({"a", "b", "a"});
    check(c1.size() == 1 && c1[0] == std::vector<size_t>({0, 2}),
          "duplicate stem -> one group with ascending indices");

    const auto c2 = findOutputStemConflicts({"Foo", "foo"});
    check(c2.size() == 1 && c2[0] == std::vector<size_t>({0, 1}),
          "case-only difference collides (case-insensitive fs)");

    const auto c3 = findOutputStemConflicts({"x", "y", "x", "y", "z", "x"});
    check(c3.size() == 2
          && c3[0] == std::vector<size_t>({0, 2, 5})
          && c3[1] == std::vector<size_t>({1, 3}),
          "two groups, ordered by first occurrence");

    const auto c4 = findOutputStemConflicts({"", ""});
    check(c4.size() == 1 && c4[0] == std::vector<size_t>({0, 1}),
          "empty stems collide too");

    // Non-ASCII case differences are NOT folded (documented limitation):
    // "\xC3\x89" = U+00C9 (E-acute), "\xC3\xA9" = U+00E9 (e-acute).
    const auto c5 = findOutputStemConflicts({"\xC3\x89t\xC3\xA9", "\xC3\xA9t\xC3\xA9"});
    check(c5.empty(), "non-ASCII case difference not folded (documented)");

    // Integration with resolve(): sanitization can merge distinct source names.
    FilenameTemplate t;
    t.sanitizeSpaces = true;
    t.segments.push_back({ FilenameToken{ FilenameToken::Type::OriginalName }, "" });
    const std::string s1 = t.resolve("a b", 1);
    const std::string s2 = t.resolve("a_b", 2);
    check(s1 == s2 && findOutputStemConflicts({s1, s2}).size() == 1,
          "sanitizeSpaces merging detected via resolved stems");

    std::printf(failures == 0 ? "\nALL PASS\n" : "\n%d FAILURE(S)\n", failures);
    return failures == 0 ? 0 : 1;
}