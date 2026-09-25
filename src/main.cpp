#include "uttt.hpp"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

using namespace uttt;

static void usage(const char* argv0) {
    std::cout
        << "Ultimate Tic-Tac-Toe exact DFPN solver (closed-board rules)\n\n"
        << "Usage: " << argv0 << " [options]\n"
        << "  --moves \"ee ea ...\"   Apply move sequence; board/cell are letters a..i\n"
        << "  --target X|O           Prove whether that player can force a win (default X)\n"
        << "  --both                 Run X-target and O-target searches and combine if exact\n"
        << "  --time SECONDS         Per-search time limit; 0 = unlimited (default 10)\n"
        << "  --max-nodes N          Unique TT-node cap; 0 = unlimited (default 0)\n"
        << "  --no-symmetry          Disable diagonal-D4 canonicalization\n"
        << "  --no-one-board-oracle  Disable exact one-open-board oracle\n"
        << "  --count-depth N        Enumerate exact canonical position counts through ply N\n"
        << "  --self-test            Run engine/symmetry/oracle tests\n"
        << "  --load-tt FILE         Resume a compatible DFPN checkpoint if FILE exists\n"
        << "  --save-tt FILE         Save DFPN checkpoint after the search (including UNKNOWN)\n"
        << "  --show                 Render the requested position\n"
        << "  -h, --help             Show this help\n";
}

static std::string result_name(ProofResult r) {
    switch (r) {
        case ProofResult::Proven: return "PROVEN";
        case ProofResult::Disproven: return "DISPROVEN";
        case ProofResult::Unknown: return "UNKNOWN";
    }
    return "?";
}

static void print_result(const Game& game, const SolveResult& r) {
    std::cout << "target=" << player_char(r.target) << " result=" << result_name(r.result) << '\n';
    const auto& s = r.stats;
    std::cout << "seconds=" << s.seconds
              << " tt=" << s.peak_tt
              << " inserts=" << s.tt_inserts
              << " hits=" << s.tt_hits
              << " calls=" << s.dfpn_calls
              << " expanded=" << s.expanded
              << " children=" << s.generated_children
              << " sym_dups=" << s.symmetry_duplicates
              << " terminal=" << s.terminal_nodes
              << " oracle_calls=" << s.oracle_calls
              << " oracle_nodes=" << s.oracle_nodes << '\n';
    if (!r.principal_variation.empty()) {
        std::cout << "pv:";
        for (auto m : r.principal_variation) std::cout << ' ' << game.move_to_string(m);
        std::cout << '\n';
    }
}

int main(int argc, char** argv) {
    Game game;
    SolverConfig cfg;
    cfg.time_limit_seconds = 10.0;
    Player target = Player::X;
    bool both = false, self_test = false, show = false;
    int count_depth = -1;
    std::string moves_text, load_tt, save_tt;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        auto need = [&](const char* opt) -> std::string {
            if (i + 1 >= argc) { std::cerr << opt << " needs a value\n"; std::exit(2); }
            return argv[++i];
        };
        if (a == "-h" || a == "--help") { usage(argv[0]); return 0; }
        else if (a == "--moves") moves_text = need("--moves");
        else if (a == "--target") {
            std::string v = need("--target");
            if (v == "X" || v == "x") target = Player::X;
            else if (v == "O" || v == "o") target = Player::O;
            else { std::cerr << "--target must be X or O\n"; return 2; }
        }
        else if (a == "--both") both = true;
        else if (a == "--time") cfg.time_limit_seconds = std::stod(need("--time"));
        else if (a == "--max-nodes") cfg.max_unique_nodes = std::stoull(need("--max-nodes"));
        else if (a == "--no-symmetry") cfg.symmetry = false;
        else if (a == "--no-one-board-oracle") cfg.one_board_oracle = false;
        else if (a == "--count-depth") count_depth = std::stoi(need("--count-depth"));
        else if (a == "--self-test") self_test = true;
        else if (a == "--load-tt") load_tt = need("--load-tt");
        else if (a == "--save-tt") save_tt = need("--save-tt");
        else if (a == "--show") show = true;
        else { std::cerr << "unknown option: " << a << '\n'; usage(argv[0]); return 2; }
    }

    if (self_test) {
        std::string message;
        bool ok = run_self_tests(game, message);
        std::cout << (ok ? "PASS: " : "FAIL: ") << message << '\n';
        if (!ok) return 1;
        if (count_depth < 0 && moves_text.empty() && !show) return 0;
    }

    if (count_depth >= 0) {
        auto counts = canonical_position_counts(game, count_depth);
        for (size_t d = 0; d < counts.size(); ++d) std::cout << d << ' ' << counts[d] << '\n';
        return 0;
    }

    State state;
    if (!moves_text.empty()) {
        std::istringstream is(moves_text);
        std::string tok;
        int ply = 0;
        while (is >> tok) {
            auto m = game.parse_move(tok);
            if (!m) { std::cerr << "bad move token: " << tok << '\n'; return 2; }
            if (!game.is_legal(state, *m)) {
                std::cerr << "illegal move at ply " << ply << ": " << tok << "\n" << game.render(state);
                return 2;
            }
            state = game.play(state, *m);
            ++ply;
        }
    }

    if (show) std::cout << game.render(state);

    if (!both) {
        DFPNSolver solver(game, cfg, target);
        if (!load_tt.empty() && std::filesystem::exists(load_tt)) {
            std::string msg;
            if (!solver.load_checkpoint(load_tt, msg)) {
                std::cerr << "checkpoint load failed: " << msg << '\n';
                return 2;
            }
            std::cout << "checkpoint: " << msg << '\n';
        }
        auto r = solver.solve(state);
        print_result(game, r);
        if (!save_tt.empty()) {
            std::string msg;
            std::filesystem::path p(save_tt);
            if (p.has_parent_path()) std::filesystem::create_directories(p.parent_path());
            if (!solver.save_checkpoint(save_tt, msg)) {
                std::cerr << "checkpoint save failed: " << msg << '\n';
                return 2;
            }
            std::cout << "checkpoint: " << msg << '\n';
        }
        return r.result == ProofResult::Unknown ? 3 : 0;
    }

    if (!load_tt.empty() || !save_tt.empty()) {
        std::cerr << "--load-tt/--save-tt are target-specific; use --target X or --target O, not --both\n";
        return 2;
    }

    DFPNSolver sx(game, cfg, Player::X);
    auto rx = sx.solve(state);
    print_result(game, rx);
    if (rx.result == ProofResult::Proven) {
        std::cout << "game-theoretic conclusion: X can force a win\n";
        return 0;
    }

    DFPNSolver so(game, cfg, Player::O);
    auto ro = so.solve(state);
    print_result(game, ro);
    if (ro.result == ProofResult::Proven) {
        std::cout << "game-theoretic conclusion: O can force a win\n";
        return 0;
    }
    if (rx.result == ProofResult::Disproven && ro.result == ProofResult::Disproven) {
        std::cout << "game-theoretic conclusion: draw under perfect play\n";
        return 0;
    }
    std::cout << "game-theoretic conclusion: unresolved within limits\n";
    return 3;
}
