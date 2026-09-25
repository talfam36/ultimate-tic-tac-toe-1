#include "uttt.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <limits>
#include <stdexcept>

namespace uttt {

DFPNSolver::DFPNSolver(const Game& game, SolverConfig cfg, Player target)
    : game_(game), cfg_(cfg), target_(target) {
    if (cfg_.reserve_tt) tt_.reserve(cfg_.reserve_tt);
    oracle_memo_.reserve(1u << 14);
}

namespace {
constexpr char CHECKPOINT_MAGIC[8] = {'U','T','T','T','D','F','P','N'};
constexpr uint32_t CHECKPOINT_VERSION = 1;

template <class T>
bool write_bin(std::ostream& os, const T& v) {
    os.write(reinterpret_cast<const char*>(&v), sizeof(T));
    return static_cast<bool>(os);
}

template <class T>
bool read_bin(std::istream& is, T& v) {
    is.read(reinterpret_cast<char*>(&v), sizeof(T));
    return static_cast<bool>(is);
}
}

bool DFPNSolver::save_checkpoint(const std::string& path, std::string& message) const {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) { message = "cannot open " + path + " for writing"; return false; }
    out.write(CHECKPOINT_MAGIC, sizeof(CHECKPOINT_MAGIC));
    const uint32_t version = CHECKPOINT_VERSION;
    const uint8_t target = static_cast<uint8_t>(target_);
    const uint8_t symmetry = cfg_.symmetry ? 1 : 0;
    const uint8_t one_board = cfg_.one_board_oracle ? 1 : 0;
    const uint8_t reserved = 0;
    const uint64_t count = static_cast<uint64_t>(tt_.size());
    if (!write_bin(out, version) || !write_bin(out, target) || !write_bin(out, symmetry) ||
        !write_bin(out, one_board) || !write_bin(out, reserved) || !write_bin(out, count)) {
        message = "failed writing checkpoint header"; return false;
    }
    for (const auto& [k, e] : tt_) {
        for (uint64_t w : k.w) if (!write_bin(out, w)) { message = "failed writing checkpoint key"; return false; }
        if (!write_bin(out, e.pn) || !write_bin(out, e.dn)) {
            message = "failed writing checkpoint bounds"; return false;
        }
    }
    out.flush();
    if (!out) { message = "failed flushing checkpoint"; return false; }
    message = "saved " + std::to_string(count) + " TT entries to " + path;
    return true;
}

bool DFPNSolver::load_checkpoint(const std::string& path, std::string& message) {
    std::ifstream in(path, std::ios::binary);
    if (!in) { message = "cannot open " + path + " for reading"; return false; }
    char magic[8]{};
    in.read(magic, sizeof(magic));
    uint32_t version = 0;
    uint8_t target = 0, symmetry = 0, one_board = 0, reserved = 0;
    uint64_t count = 0;
    if (!in || std::memcmp(magic, CHECKPOINT_MAGIC, sizeof(magic)) != 0 ||
        !read_bin(in, version) || !read_bin(in, target) || !read_bin(in, symmetry) ||
        !read_bin(in, one_board) || !read_bin(in, reserved) || !read_bin(in, count)) {
        message = "invalid or truncated checkpoint header"; return false;
    }
    (void)reserved;
    if (version != CHECKPOINT_VERSION) {
        message = "unsupported checkpoint version " + std::to_string(version); return false;
    }
    if (target != static_cast<uint8_t>(target_)) {
        message = "checkpoint target does not match requested target"; return false;
    }
    if (symmetry != static_cast<uint8_t>(cfg_.symmetry ? 1 : 0) ||
        one_board != static_cast<uint8_t>(cfg_.one_board_oracle ? 1 : 0)) {
        message = "checkpoint proof configuration does not match current solver"; return false;
    }
    const auto data_begin = in.tellg();
    in.seekg(0, std::ios::end);
    const auto data_end = in.tellg();
    in.seekg(data_begin);
    if (data_begin < 0 || data_end < data_begin ||
        static_cast<uint64_t>(data_end - data_begin) != count * 32ull) {
        message = "checkpoint size does not match entry count"; return false;
    }
    tt_.clear();
    if (count > 0 && count < static_cast<uint64_t>(tt_.max_size())) {
        const uint64_t reserve = std::min<uint64_t>(count + count/4 + 1, 100'000'000ull);
        tt_.reserve(static_cast<size_t>(reserve));
    }
    for (uint64_t i = 0; i < count; ++i) {
        Key k;
        Entry e;
        if (!read_bin(in, k.w[0]) || !read_bin(in, k.w[1]) || !read_bin(in, k.w[2]) ||
            !read_bin(in, e.pn) || !read_bin(in, e.dn)) {
            tt_.clear();
            message = "truncated checkpoint body"; return false;
        }
        if (e.pn > INF || e.dn > INF || (e.pn == 0 && e.dn == 0)) {
            tt_.clear();
            message = "checkpoint contains invalid proof bounds"; return false;
        }
        tt_.emplace(k, e);
    }
    message = "loaded " + std::to_string(tt_.size()) + " TT entries from " + path;
    return true;
}

Key DFPNSolver::key_of(const State& s) const {
    return cfg_.symmetry ? game_.canonical_key(s) : game_.raw_key(s);
}

uint32_t DFPNSolver::sat_add(uint32_t a, uint32_t b) {
    if (a >= INF || b >= INF || a > INF - b) return INF;
    return a + b;
}

void DFPNSolver::check_stop() {
    if (cfg_.max_unique_nodes && stats_.tt_inserts >= cfg_.max_unique_nodes) throw SearchStopped{};
    if (cfg_.time_limit_seconds > 0) {
        auto now = std::chrono::steady_clock::now();
        double sec = std::chrono::duration<double>(now - start_).count();
        if (sec >= cfg_.time_limit_seconds) throw SearchStopped{};
    }
}

std::optional<bool> DFPNSolver::exact_static_value(const State& s) {
    switch (game_.outcome(s)) {
        case Outcome::XWin: ++stats_.terminal_nodes; return target_ == Player::X;
        case Outcome::OWin: ++stats_.terminal_nodes; return target_ == Player::O;
        case Outcome::Draw: ++stats_.terminal_nodes; return false;
        case Outcome::InPlay: break;
    }
    if (cfg_.one_board_oracle && game_.open_board_count(s) == 1) {
        ++stats_.oracle_calls;
        return one_board_target_win(s);
    }
    return std::nullopt;
}

bool DFPNSolver::one_board_target_win(const State& s) {
    return one_board_target_win_rec(s);
}

bool DFPNSolver::one_board_target_win_rec(const State& s) {
    check_stop();
    switch (game_.outcome(s)) {
        case Outcome::XWin: return target_ == Player::X;
        case Outcome::OWin: return target_ == Player::O;
        case Outcome::Draw: return false;
        case Outcome::InPlay: break;
    }
    Key k = key_of(s);
    if (auto it = oracle_memo_.find(k); it != oracle_memo_.end()) return it->second;
    ++stats_.oracle_nodes;
    auto moves = game_.legal_moves(s);
    bool answer;
    if (s.turn == target_) {
        answer = false;
        for (Move m : moves) {
            if (one_board_target_win_rec(game_.play(s, m))) { answer = true; break; }
        }
    } else {
        answer = true;
        for (Move m : moves) {
            if (!one_board_target_win_rec(game_.play(s, m))) { answer = false; break; }
        }
    }
    oracle_memo_.emplace(k, answer);
    return answer;
}

DFPNSolver::Entry DFPNSolver::ensure_entry(const State& s, const Key& key) {
    if (auto it = tt_.find(key); it != tt_.end()) {
        ++stats_.tt_hits;
        return it->second;
    }
    check_stop();
    Entry e{1,1};
    if (auto v = exact_static_value(s)) e = *v ? Entry{0, INF} : Entry{INF, 0};
    tt_.emplace(key, e);
    ++stats_.tt_inserts;
    stats_.peak_tt = std::max(stats_.peak_tt, tt_.size());
    return e;
}

std::vector<DFPNSolver::Child> DFPNSolver::children(const State& s) {
    auto moves = game_.legal_moves(s);
    std::vector<Child> out;
    out.reserve(moves.size());
    std::unordered_set<Key, KeyHash> seen;
    if (cfg_.symmetry) seen.reserve(moves.size()*2 + 1);

    auto [before_x, before_o] = game_.global_claims(s);
    uint16_t before_claim = s.turn == Player::X ? before_x : before_o;

    for (Move m : moves) {
        State ns = game_.play(s, m);
        Key k = key_of(ns);
        if (cfg_.symmetry && !seen.emplace(k).second) {
            ++stats_.symmetry_duplicates;
            continue;
        }
        int score = 0;
        Outcome oc = game_.outcome(ns);
        if ((oc == Outcome::XWin && s.turn == Player::X) || (oc == Outcome::OWin && s.turn == Player::O)) score += 1'000'000;
        auto [ax, ao] = game_.global_claims(ns);
        uint16_t after_claim = s.turn == Player::X ? ax : ao;
        if (after_claim != before_claim) score += 20'000;
        if (m.cell == 4) score += 300;
        else if (m.cell == 0 || m.cell == 2 || m.cell == 6 || m.cell == 8) score += 150;
        if (ns.next_board < 0) score -= 200;
        out.push_back(Child{m, ns, k, score});
    }
    std::sort(out.begin(), out.end(), [](const Child& a, const Child& b) {
        return a.order_score > b.order_score;
    });
    stats_.generated_children += out.size();
    return out;
}

DFPNSolver::Entry DFPNSolver::aggregate(const State& s, const std::vector<Child>& ch) {
    if (ch.empty()) {
        auto v = exact_static_value(s);
        return v.value_or(false) ? Entry{0, INF} : Entry{INF, 0};
    }
    bool or_node = (s.turn == target_);
    Entry e;
    if (or_node) {
        e.pn = INF; e.dn = 0;
        for (const auto& c : ch) {
            auto it = tt_.find(c.key);
            assert(it != tt_.end());
            e.pn = std::min(e.pn, it->second.pn);
            e.dn = sat_add(e.dn, it->second.dn);
        }
    } else {
        e.pn = 0; e.dn = INF;
        for (const auto& c : ch) {
            auto it = tt_.find(c.key);
            assert(it != tt_.end());
            e.pn = sat_add(e.pn, it->second.pn);
            e.dn = std::min(e.dn, it->second.dn);
        }
    }
    return e;
}

void DFPNSolver::dfpn(const State& s, uint32_t thpn, uint32_t thdn) {
    ++stats_.dfpn_calls;
    if ((stats_.dfpn_calls & 0x3FFu) == 0) check_stop();

    Key sk = key_of(s);
    Entry cur = ensure_entry(s, sk);
    if (cur.pn == 0 || cur.dn == 0 || cur.pn >= thpn || cur.dn >= thdn) return;

    auto ch = children(s);
    ++stats_.expanded;
    for (auto& c : ch) ensure_entry(c.state, c.key);

    while (true) {
        cur = aggregate(s, ch);
        tt_[sk] = cur;
        if (cur.pn == 0 || cur.dn == 0 || cur.pn >= thpn || cur.dn >= thdn) return;
        check_stop();

        bool or_node = (s.turn == target_);
        size_t best = 0;
        uint32_t second = INF;

        if (or_node) {
            uint32_t bestpn = INF;
            for (size_t i = 0; i < ch.size(); ++i) {
                const Entry ce = tt_.at(ch[i].key);
                if (ce.pn < bestpn) {
                    second = bestpn;
                    bestpn = ce.pn;
                    best = i;
                } else if (ce.pn < second) second = ce.pn;
            }
            Entry ce = tt_.at(ch[best].key);
            uint32_t child_thpn = std::min(thpn, second >= INF-1 ? INF : second + 1);
            uint64_t td = static_cast<uint64_t>(thdn) - cur.dn + ce.dn;
            uint32_t child_thdn = static_cast<uint32_t>(std::min<uint64_t>(INF, td));
            dfpn(ch[best].state, child_thpn, child_thdn);
        } else {
            uint32_t bestdn = INF;
            for (size_t i = 0; i < ch.size(); ++i) {
                const Entry ce = tt_.at(ch[i].key);
                if (ce.dn < bestdn) {
                    second = bestdn;
                    bestdn = ce.dn;
                    best = i;
                } else if (ce.dn < second) second = ce.dn;
            }
            Entry ce = tt_.at(ch[best].key);
            uint64_t tp = static_cast<uint64_t>(thpn) - cur.pn + ce.pn;
            uint32_t child_thpn = static_cast<uint32_t>(std::min<uint64_t>(INF, tp));
            uint32_t child_thdn = std::min(thdn, second >= INF-1 ? INF : second + 1);
            dfpn(ch[best].state, child_thpn, child_thdn);
        }
    }
}

std::vector<Move> DFPNSolver::extract_pv(const State& root, size_t max_len) {
    std::vector<Move> pv;
    State s = root;
    for (size_t depth = 0; depth < max_len && game_.outcome(s) == Outcome::InPlay; ++depth) {
        auto ch = children(s);
        if (ch.empty()) break;
        for (auto& c : ch) ensure_entry(c.state, c.key);
        std::optional<Child> chosen;
        if (tt_.at(key_of(s)).pn == 0) {
            for (auto& c : ch) if (tt_.at(c.key).pn == 0) { chosen = c; break; }
        } else {
            for (auto& c : ch) if (tt_.at(c.key).dn == 0) { chosen = c; break; }
        }
        if (!chosen) break;
        pv.push_back(chosen->move);
        s = chosen->state;
    }
    return pv;
}

SolveResult DFPNSolver::solve(const State& root) {
    stats_ = {};
    oracle_memo_.clear();
    if (cfg_.reserve_tt) tt_.reserve(cfg_.reserve_tt);
    start_ = std::chrono::steady_clock::now();
    ProofResult result = ProofResult::Unknown;
    try {
        Key rk = key_of(root);
        ensure_entry(root, rk);
        dfpn(root, INF, INF);
        Entry e = tt_.at(rk);
        if (e.pn == 0) result = ProofResult::Proven;
        else if (e.dn == 0) result = ProofResult::Disproven;
    } catch (const SearchStopped&) {
        result = ProofResult::Unknown;
    }
    stats_.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start_).count();
    SolveResult out;
    out.result = result;
    out.target = target_;
    out.stats = stats_;
    if (result != ProofResult::Unknown) {
        try { out.principal_variation = extract_pv(root); }
        catch (...) { out.principal_variation.clear(); }
    }
    return out;
}

} // namespace uttt
