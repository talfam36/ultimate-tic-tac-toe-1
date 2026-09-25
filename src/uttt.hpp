#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace uttt {

enum class Player : uint8_t { X = 0, O = 1 };
inline Player other(Player p) { return p == Player::X ? Player::O : Player::X; }
inline char player_char(Player p) { return p == Player::X ? 'X' : 'O'; }

struct Move {
    uint8_t board = 0;
    uint8_t cell = 0;
    friend bool operator==(const Move&, const Move&) = default;
};

struct State {
    std::array<uint16_t, 9> x{};
    std::array<uint16_t, 9> o{};
    int8_t next_board = -1;
    Player turn = Player::X;
};

struct Key {
    std::array<uint64_t, 3> w{};
    friend bool operator==(const Key&, const Key&) = default;
    friend bool operator<(const Key& a, const Key& b) {
        if (a.w[2] != b.w[2]) return a.w[2] < b.w[2];
        if (a.w[1] != b.w[1]) return a.w[1] < b.w[1];
        return a.w[0] < b.w[0];
    }
};

struct KeyHash { size_t operator()(const Key& k) const noexcept; };

enum class Outcome : uint8_t { InPlay, XWin, OWin, Draw };

class Game {
public:
    Game();
    const std::array<std::array<uint8_t, 9>, 8>& sym_map() const { return sym_map_; }
    bool has_line(uint16_t bits) const;
    bool board_closed(const State& s, int b) const;
    int open_board_count(const State& s) const;
    std::pair<uint16_t, uint16_t> global_claims(const State& s) const;
    Outcome outcome(const State& s) const;
    std::vector<Move> legal_moves(const State& s) const;
    bool is_legal(const State& s, Move m) const;
    State play(const State& s, Move m) const;
    Key raw_key(const State& s) const;
    Key canonical_key(const State& s) const;
    std::string move_to_string(Move m) const;
    std::optional<Move> parse_move(const std::string& text) const;
    std::string render(const State& s) const;

private:
    std::array<std::array<uint8_t, 9>, 8> sym_map_{};
    std::array<std::array<uint16_t, 512>, 8> mask_xform_{};
    static constexpr std::array<uint16_t, 8> WIN_MASKS = {
        0b000000111, 0b000111000, 0b111000000,
        0b001001001, 0b010010010, 0b100100100,
        0b100010001, 0b001010100,
    };
    Key pack(const std::array<uint16_t,9>& x,
             const std::array<uint16_t,9>& o,
             int next_board, Player turn) const;
};

struct SolverConfig {
    bool symmetry = true;
    bool one_board_oracle = true;
    uint64_t max_unique_nodes = 0;
    double time_limit_seconds = 0;
    size_t reserve_tt = 1u << 20;
};

struct SolverStats {
    uint64_t dfpn_calls = 0;
    uint64_t expanded = 0;
    uint64_t generated_children = 0;
    uint64_t symmetry_duplicates = 0;
    uint64_t terminal_nodes = 0;
    uint64_t oracle_nodes = 0;
    uint64_t oracle_calls = 0;
    uint64_t tt_hits = 0;
    uint64_t tt_inserts = 0;
    size_t peak_tt = 0;
    double seconds = 0;
};

enum class ProofResult : uint8_t { Proven, Disproven, Unknown };

struct SolveResult {
    ProofResult result = ProofResult::Unknown;
    Player target = Player::X;
    SolverStats stats{};
    std::vector<Move> principal_variation;
};

class DFPNSolver {
public:
    DFPNSolver(const Game& game, SolverConfig cfg, Player target);
    SolveResult solve(const State& root);
    bool load_checkpoint(const std::string& path, std::string& message);
    bool save_checkpoint(const std::string& path, std::string& message) const;
    size_t checkpoint_entries() const { return tt_.size(); }

private:
    static constexpr uint32_t INF = 1'000'000'000u;
    struct Entry { uint32_t pn = 1; uint32_t dn = 1; };
    struct Child { Move move{}; State state{}; Key key{}; int order_score = 0; };
    struct SearchStopped {};

    const Game& game_;
    SolverConfig cfg_;
    Player target_;
    std::unordered_map<Key, Entry, KeyHash> tt_;
    std::unordered_map<Key, bool, KeyHash> oracle_memo_;
    SolverStats stats_{};
    std::chrono::steady_clock::time_point start_{};

    Key key_of(const State& s) const;
    void check_stop();
    Entry ensure_entry(const State& s, const Key& key);
    std::optional<bool> exact_static_value(const State& s);
    bool one_board_target_win(const State& s);
    bool one_board_target_win_rec(const State& s);
    std::vector<Child> children(const State& s);
    Entry aggregate(const State& s, const std::vector<Child>& ch);
    void dfpn(const State& s, uint32_t thpn, uint32_t thdn);
    static uint32_t sat_add(uint32_t a, uint32_t b);
    std::vector<Move> extract_pv(const State& root, size_t max_len = 81);
};

std::vector<uint64_t> canonical_position_counts(const Game& game, int max_depth);
bool run_self_tests(const Game& game, std::string& message);

} // namespace uttt
