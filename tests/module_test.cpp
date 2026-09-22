// Loads the real libautocorrect.so into a real fcitx5 Instance and types into a
// fake app that honours commit/delete-surrounding like a text field would.
// No compositor, no live keyboard.

#include <fcitx-utils/capabilityflags.h>
#include <fcitx-utils/eventdispatcher.h>
#include <fcitx-utils/key.h>
#include <fcitx-utils/testing.h>
#include <fcitx-utils/utf8.h>
#include <fcitx/addonmanager.h>
#include <fcitx/inputcontext.h>
#include <fcitx/inputcontextmanager.h>
#include <fcitx/inputmethodgroup.h>
#include <fcitx/inputmethodmanager.h>
#include <fcitx/instance.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

using namespace fcitx;
namespace stdfs = std::filesystem;

static int failures = 0;
#define EXPECT_EQ(a, b)                                                                   \
    do {                                                                                  \
        auto _a = (a);                                                                    \
        auto _b = (b);                                                                    \
        if (_a != _b) {                                                                   \
            std::cerr << "FAIL line " << __LINE__ << ": [" << _a << "] != [" << _b << "]\n"; \
            ++failures;                                                                   \
        }                                                                                 \
    } while (0)

// A text field: content with the cursor at the end.
class FakeApp : public InputContext {
public:
    FakeApp(InputContextManager &m, const std::string &program, bool surrounding = true)
        : InputContext(m, program), surrounding_(surrounding) {
        created();
        setCapabilityFlags(surrounding ? CapabilityFlags(CapabilityFlag::SurroundingText)
                                       : CapabilityFlags());
        focusIn();
        sync();
    }
    ~FakeApp() override { destroy(); }
    const char *frontend() const override { return "fakeapp"; }

    std::string text;
    int now = 1000; // ms; each key is 120ms after the last unless told otherwise

    void sync() {
        if (!surrounding_) return;
        auto n = utf8::length(text);
        surroundingText().setText(text, n, n);
        updateSurroundingText();
    }

    // Press one key (release too); unfiltered keys reach the "app".
    void press(const Key &key, int gapMs = 120) {
        now += gapMs;
        KeyEvent down(this, key, false, now);
        const bool filtered = keyEvent(down);
        if (std::getenv("AC_TEST_DEBUG")) std::cerr << "key " << key.toString() << " -> " << down.key().toString() << " filtered=" << filtered << " accepted=" << down.accepted() << "\n";
        if (!filtered) {
            auto sym = key.sym();
            if (sym == FcitxKey_BackSpace) {
                if (!text.empty()) text.pop_back();
            } else if (auto s = Key::keySymToUTF8(sym);
                       !s.empty() && !key.states().test(KeyState::Ctrl)) {
                text += s;
            }
        }
        KeyEvent up(this, key, true, now + 5);
        keyEvent(up);
        sync();
    }
    void type(const std::string &s, int gapMs = 120) {
        for (char c : s) {
            if (c == ' ') press(Key(FcitxKey_space), gapMs);
            else press(Key(std::string(1, c)), gapMs);
        }
    }
    void backspace() { press(Key(FcitxKey_BackSpace)); }

protected:
    void commitStringImpl(const std::string &s) override {
        text += s;
        sync();
    }
    void deleteSurroundingTextImpl(int offset, unsigned int size) override {
        // We only ever delete right before the cursor.
        if (offset != -static_cast<int>(size)) {
            std::cerr << "unexpected delete offset\n";
            ++failures;
            return;
        }
        auto n = utf8::length(text);
        auto keep = utf8::ncharByteLength(text.begin(), n - size);
        text.resize(static_cast<size_t>(keep));
        sync();
    }
    void forwardKeyImpl(const ForwardKeyEvent &) override {}
    void updatePreeditImpl() override {}

private:
    bool surrounding_;
};

static std::string slurp(const stdfs::path &p) {
    std::ifstream in(p);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void run(Instance &instance, const stdfs::path &config) {
    auto &mgr = instance.inputContextManager();

    {   // Typo fixed at a space, case kept, punctuation boundary too.
        FakeApp app(mgr, "gedit");
        app.type("teh cat ");
        EXPECT_EQ(app.text, std::string("the cat "));
        app.type("Recieve");
        app.press(Key(FcitxKey_comma));
        EXPECT_EQ(app.text, std::string("the cat Receive,"));
    }
    {   // Backspace straight after undoes, and the word is learned.
        FakeApp app(mgr, "gedit");
        app.type("teh ");
        EXPECT_EQ(app.text, std::string("the "));
        app.backspace();
        EXPECT_EQ(app.text, std::string("teh "));
        app.type("teh ");
        EXPECT_EQ(app.text, std::string("teh teh "));
        EXPECT_EQ(slurp(config / "never"), std::string("teh\n"));
    }
    {   // A later Backspace is just a Backspace.
        FakeApp app(mgr, "gedit");
        app.type("recieve x");
        app.backspace();
        app.backspace();
        EXPECT_EQ(app.text, std::string("receive"));
    }
    {   // Expansion; undo restores it but does not learn it.
        FakeApp app(mgr, "gedit");
        app.type("mha ");
        EXPECT_EQ(app.text, std::string("1 Example St, Atlanta, GA "));
        app.backspace();
        EXPECT_EQ(app.text, std::string("mha "));
        app.type("mha ");
        EXPECT_EQ(app.text, std::string("mha 1 Example St, Atlanta, GA "));
    }
    {   // Terminals are never touched.
        FakeApp app(mgr, "kitty");
        app.type("recieve ");
        EXPECT_EQ(app.text, std::string("recieve "));
    }
    {   // Password fields are never touched.
        FakeApp app(mgr, "brave-browser");
        app.setCapabilityFlags(CapabilityFlags(CapabilityFlag::SurroundingText) |
                               CapabilityFlag::Password);
        app.type("recieve ");
        EXPECT_EQ(app.text, std::string("recieve "));
    }
    {   // Injected bursts (dictation, wtype) are left alone.
        FakeApp app(mgr, "gedit");
        app.type("recieve ", 2);
        EXPECT_EQ(app.text, std::string("recieve "));
    }
    {   // A word that started mid-word (cursor moved in with the mouse).
        FakeApp app(mgr, "gedit");
        app.text = "xx";
        app.sync();
        app.type("recieve ");
        EXPECT_EQ(app.text, std::string("xxrecieve "));
    }
    {   // The app's text disagrees with what we saw typed: do nothing.
        FakeApp app(mgr, "gedit");
        app.type("recieve");
        app.text = "something else";
        app.sync();
        app.press(Key(FcitxKey_space));
        EXPECT_EQ(app.text, std::string("something else "));
    }
    {   // No surrounding text support: fail safe, no fix.
        FakeApp app(mgr, "someelectronapp", /*surrounding=*/false);
        app.type("recieve ");
        EXPECT_EQ(app.text, std::string("recieve "));
    }
    {   // Ctrl combos reset the word.
        FakeApp app(mgr, "gedit");
        app.type("reci");
        app.press(Key("Control+a"));
        app.type("eve ");
        EXPECT_EQ(app.text, std::string("recieve "));
    }
    {   // The OFF switch.
        std::ofstream(config / "off").close();
        FakeApp app(mgr, "gedit");
        app.type("recieve ");
        EXPECT_EQ(app.text, std::string("recieve "));
        stdfs::remove(config / "off");
        app.type("recieve ");
        EXPECT_EQ(app.text, std::string("recieve receive "));
    }
}

int main(int, char **argv) {
    const stdfs::path build = stdfs::absolute(stdfs::path(argv[0])).parent_path();
    const stdfs::path home = build / "module_test_home";
    stdfs::remove_all(home);
    const stdfs::path config = home / "config/autocorrect";
    const stdfs::path data = home / "data/autocorrect.omarchy";
    stdfs::create_directories(config);
    stdfs::create_directories(data);
    std::ofstream(data / "typomap.tsv") << "teh\tthe\nrecieve\treceive\n";
    std::ofstream(config / "expansions") << "mha\t1 Example St, Atlanta, GA\n";
    setenv("XDG_CONFIG_HOME", (home / "config").c_str(), 1);
    setenv("XDG_DATA_HOME", (home / "data").c_str(), 1);

    setupTestingEnvironmentPath(build, {"."}, {"testdata"});

    char arg0[] = "module_test";
    char arg1[] = "--disable=all";
    char arg2[] = "--enable=testim,autocorrect";
    char arg2b[] = "--enable=testim";
    char *args[] = {arg0, arg1, std::getenv("AC_TEST_NOADDON") ? arg2b : arg2};
    Instance instance(3, args);
    instance.addonManager().registerDefaultLoader(nullptr);
    instance.eventDispatcher().schedule([&instance, config]() {
        auto &im = instance.inputMethodManager();
        InputMethodGroup group(im.currentGroup().name());
        group.inputMethodList().emplace_back("keyboard-us"); // provided by testim
        group.setDefaultInputMethod("keyboard-us");
        group.setDefaultLayout("us"); // same as vic; otherwise keys go through a keycode-only xkb path
        im.setGroup(std::move(group));
        if (!std::getenv("AC_TEST_NOADDON") && !instance.addonManager().addon("autocorrect", true)) {
            std::cerr << "FAIL: autocorrect addon did not load\n";
            ++failures;
        } else {
            run(instance, config);
        }
        instance.exit();
    });
    instance.exec();
    if (failures == 0) std::cout << "module_test: all passed\n";
    return failures == 0 ? 0 : 1;
}
