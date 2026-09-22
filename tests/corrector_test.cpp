#include "corrector.h"

#include <cstdio>
#include <fstream>
#include <iostream>
#include <string>

using namespace autocorrect;

static int failures = 0;
#define EXPECT(cond)                                                              \
    do {                                                                          \
        if (!(cond)) {                                                            \
            std::cerr << "FAIL " << __LINE__ << ": " #cond "\n";                  \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

static std::string writeTemp(const char *name, const char *body) {
    std::string path = std::string("/tmp/autocorrect_test_") + name;
    std::ofstream(path) << body;
    return path;
}

int main() {
    Corrector c;
    EXPECT(c.loadTypoMap(writeTemp("typos", "# header\nteh\tthe\nrecieve\treceive\n"
                                            "amercia\tAmerica\nbroken-line\n\n")) == 3);
    EXPECT(c.loadExpansions(writeTemp("exp", "mha\t1 Example St, Atlanta, GA\n")) == 1);

    // Typo fixes follow the typed case.
    EXPECT(c.lookup("teh")->text == "the");
    EXPECT(c.lookup("Teh")->text == "The");
    EXPECT(c.lookup("TEH")->text == "THE");
    EXPECT(!c.lookup("teh")->isExpansion);
    // A proper-noun fix keeps its own capital.
    EXPECT(c.lookup("amercia")->text == "America");
    // Unknown words are left alone.
    EXPECT(!c.lookup("the"));
    EXPECT(!c.lookup(""));

    // Expansions are verbatim and flagged.
    EXPECT(c.lookup("mha")->text == "1 Example St, Atlanta, GA");
    EXPECT(c.lookup("MHA")->isExpansion);

    // Never-list beats a typo fix, but not an expansion.
    c.addNever("Teh");
    EXPECT(!c.lookup("teh"));
    EXPECT(c.isNever("TEH"));
    c.addNever("mha");
    EXPECT(c.lookup("mha"));

    // Reloading expansions replaces them.
    EXPECT(c.loadExpansions(writeTemp("exp2", "brb\tbe right back\n")) == 1);
    EXPECT(!c.lookup("mha"));
    EXPECT(c.lookup("brb")->text == "be right back");

    // matchCase edge cases.
    EXPECT(matchCase("I", "i'm") == "I'm");
    EXPECT(matchCase("dont", "don't") == "don't");

    // Deny-list: exact and long-prefix only.
    EXPECT(isDeniedProgram("kitty"));
    EXPECT(isDeniedProgram("Kitty"));
    EXPECT(isDeniedProgram("kittyfloat"));
    EXPECT(isDeniedProgram("com.mitchellh.ghostty"));
    EXPECT(isDeniedProgram("st"));
    EXPECT(!isDeniedProgram("steam"));      // smartcomplete's "st" prefix bug
    EXPECT(!isDeniedProgram("strata"));
    EXPECT(!isDeniedProgram("brave-browser"));
    EXPECT(!isDeniedProgram(""));
    EXPECT(isDeniedProgram("code", {"code"}));

    if (failures == 0) std::cout << "corrector_test: all passed\n";
    return failures == 0 ? 0 : 1;
}
