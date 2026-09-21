// EXCERPT from smartcomplete @ b14aed4, MIT (see third_party/smartcomplete/LICENSE)
// src/predictor/predictor.h:38-52 (default terminal blocklist)
    std::string dict_dir;
    std::string user_dict_path;

    // Programs where SmartComplete should stay out of the way (no predictions,
    // pure pass-through). Default list covers common terminal emulators and shells.
    std::vector<std::string> disabled_programs = {
        "kitty", "alacritty", "foot", "wezterm", "wezterm-gui",
        "xterm", "urxvt", "rxvt", "st",
        "gnome-terminal", "gnome-terminal-server",
        "konsole", "xfce4-terminal", "lxterminal",
        "mate-terminal", "deepin-terminal", "terminator",
        "tilix", "hyper", "terminology", "blackbox",
        "ptyxis", "cool-retro-term", "termite",
        "zsh", "bash", "fish", "tmux", "screen"
    };

// src/predictor/predictor.cpp:633-645 (is_program_disabled)
bool Predictor::is_program_disabled(const std::string& program) const {
    if (program.empty()) return false;
    // Case-insensitive match: exact match, or pattern is a prefix of program name.
    // Prefix match catches custom window class variants like "kittyfloat", "kittyterm", etc.
    const std::string prog_lower = text_utils::to_lower_ascii(program);
    for (const auto& p : config_.disabled_programs) {
        const std::string pat = text_utils::to_lower_ascii(p);
        if (pat.empty()) continue;
        if (pat == prog_lower) return true;
        if (prog_lower.rfind(pat, 0) == 0) return true; // starts with pattern
    }
    return false;
}
