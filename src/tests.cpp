#include "uttt.hpp"

#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <unordered_set>

namespace uttt {

std::vector<uint64_t> canonical_position_counts(const Game& game, int max_depth) {
    std::vector<uint64_t> counts;
    std::unordered_map<Key, State, KeyHash> layer;
    State root;
    layer.emplace(game.canonical_key(root), root);
    counts.push_back(1);
    for (int d = 0; d < max_depth; ++d) {
        std::unordered_map<Key, State, KeyHash> next;
        next.reserve(layer.size() * 8);
        for (const auto& [k, s] : layer) {
            (void)k;
            for (Move m : game.legal_moves(s)) {
                State ns = game.play(s, m);
                Key nk = game.canonical_key(ns);
                next.try_emplace(nk, ns);
            }
        }
        counts.push_back(next.size());
        layer.swap(next);
    }
    return counts;
}

bool run_self_tests(const Game& game, std::string& message) {
    try {
        State s;
        if (game.legal_moves(s).size() != 81) throw std::runtime_error("initial legal move count != 81");
        auto ee = game.parse_move("ee");
        if (!ee) throw std::runtime_error("parse ee failed");
        s = game.play(s, *ee);
        if (s.next_board != 4) throw std::runtime_error("ee did not route to center board");
        if (game.legal_moves(s).size() != 8) throw std::runtime_error("after ee legal move count != 8");

        State init;
        std::unordered_set<Key,KeyHash> first;
        for (Move m : game.legal_moves(init)) first.insert(game.canonical_key(game.play(init,m)));
        if (first.size() != 15) throw std::runtime_error("first-ply symmetry classes != 15");

        std::unordered_set<Key,KeyHash> replies;
        for (Move m : game.legal_moves(s)) replies.insert(game.canonical_key(game.play(s,m)));
        if (replies.size() != 2) throw std::runtime_error("center-center reply classes != 2");

        auto counts = canonical_position_counts(game, 4);
        const std::vector<uint64_t> expected{1,15,102,822,6920};
        if (counts != expected) {
            std::ostringstream os;
            os << "canonical counts mismatch: got";
            for (auto n : counts) os << ' ' << n;
            throw std::runtime_error(os.str());
        }

        State e;
        const uint16_t win = 0b000000111;
        for (int b = 0; b < 8; ++b) {
            if (b == 0 || b == 4 || b == 7) e.x[b] = win;
            else e.o[b] = win;
        }
        e.turn = Player::X;
        e.next_board = -1;
        if (game.outcome(e) != Outcome::InPlay || game.open_board_count(e) != 1) {
            e = State{};
            const uint16_t dx = static_cast<uint16_t>((1u<<0)|(1u<<2)|(1u<<3)|(1u<<7));
            const uint16_t do_ = static_cast<uint16_t>(0x1FFu ^ dx);
            for (int b = 0; b < 8; ++b) { e.x[b] = dx; e.o[b] = do_; }
        }
        if (game.open_board_count(e) != 1) throw std::runtime_error("one-open-board fixture failed");
        SolverConfig cfg; cfg.time_limit_seconds = 2; cfg.max_unique_nodes = 100000;
        DFPNSolver sx(game, cfg, Player::X);
        auto rr = sx.solve(e);
        if (rr.result == ProofResult::Unknown) throw std::runtime_error("one-board oracle did not resolve fixture");

        State ttt;
        const uint16_t draw_x = static_cast<uint16_t>((1u<<0)|(1u<<2)|(1u<<3)|(1u<<7)|(1u<<8));
        const uint16_t draw_o = static_cast<uint16_t>(0x1FFu ^ draw_x);
        for (int b = 0; b < 8; ++b) { ttt.x[b] = draw_x; ttt.o[b] = draw_o; }
        ttt.next_board = 8;
        ttt.turn = Player::X;
        SolverConfig raw_cfg; raw_cfg.one_board_oracle = false; raw_cfg.time_limit_seconds = 5; raw_cfg.max_unique_nodes = 500000;
        DFPNSolver tx(game, raw_cfg, Player::X);
        DFPNSolver to(game, raw_cfg, Player::O);
        if (tx.solve(ttt).result != ProofResult::Disproven) throw std::runtime_error("DFPN failed embedded TTT draw for X target");
        if (to.solve(ttt).result != ProofResult::Disproven) throw std::runtime_error("DFPN failed embedded TTT draw for O target");

        State winfix;
        for (int b = 0; b < 6; ++b) { winfix.x[b] = draw_x; winfix.o[b] = draw_o; }
        winfix.x[6] = win;
        winfix.x[7] = win;
        winfix.x[8] = static_cast<uint16_t>((1u<<0)|(1u<<1));
        winfix.o[8] = static_cast<uint16_t>((1u<<3)|(1u<<4));
        winfix.next_board = 8;
        winfix.turn = Player::X;
        DFPNSolver wx(game, raw_cfg, Player::X);
        if (wx.solve(winfix).result != ProofResult::Proven) throw std::runtime_error("DFPN failed immediate macro-win fixture");

        message = "all self-tests passed";
        return true;
    } catch (const std::exception& ex) {
        message = ex.what();
        return false;
    }
}

} // namespace uttt
