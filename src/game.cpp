#include "uttt.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>

namespace uttt {

size_t KeyHash::operator()(const Key& k) const noexcept {
    auto mix = [](uint64_t z) {
        z += 0x9e3779b97f4a7c15ULL;
        z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
        z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
        return z ^ (z >> 31);
    };
    return static_cast<size_t>(mix(k.w[0]) ^ (mix(k.w[1]) << 1) ^ (mix(k.w[2]) >> 1));
}

Game::Game() {
    for (int g = 0; g < 8; ++g) {
        for (int i = 0; i < 9; ++i) {
            int r = i / 3, c = i % 3;
            if (g >= 4) c = 2 - c;
            int rot = g & 3;
            for (int k = 0; k < rot; ++k) {
                int nr = c;
                int nc = 2 - r;
                r = nr; c = nc;
            }
            sym_map_[g][i] = static_cast<uint8_t>(3*r + c);
        }
        for (int m = 0; m < 512; ++m) {
            uint16_t out = 0;
            for (int i = 0; i < 9; ++i) {
                if (m & (1 << i)) out |= static_cast<uint16_t>(1u << sym_map_[g][i]);
            }
            mask_xform_[g][m] = out;
        }
    }
}

bool Game::has_line(uint16_t bits) const {
    for (auto m : WIN_MASKS) if ((bits & m) == m) return true;
    return false;
}

bool Game::board_closed(const State& s, int b) const {
    return has_line(s.x[b]) || has_line(s.o[b]) || ((s.x[b] | s.o[b]) == 0x1FFu);
}

int Game::open_board_count(const State& s) const {
    int n = 0;
    for (int b = 0; b < 9; ++b) if (!board_closed(s, b)) ++n;
    return n;
}

std::pair<uint16_t,uint16_t> Game::global_claims(const State& s) const {
    uint16_t gx = 0, go = 0;
    for (int b = 0; b < 9; ++b) {
        if (has_line(s.x[b])) gx |= static_cast<uint16_t>(1u << b);
        else if (has_line(s.o[b])) go |= static_cast<uint16_t>(1u << b);
    }
    return {gx, go};
}

Outcome Game::outcome(const State& s) const {
    auto [gx, go] = global_claims(s);
    if (has_line(gx)) return Outcome::XWin;
    if (has_line(go)) return Outcome::OWin;
    if (open_board_count(s) == 0) return Outcome::Draw;
    return Outcome::InPlay;
}

std::vector<Move> Game::legal_moves(const State& s) const {
    std::vector<Move> out;
    if (outcome(s) != Outcome::InPlay) return out;
    auto add_board = [&](int b) {
        if (board_closed(s, b)) return;
        uint16_t occ = s.x[b] | s.o[b];
        for (int c = 0; c < 9; ++c) if (!(occ & (1u << c))) {
            out.push_back(Move{static_cast<uint8_t>(b), static_cast<uint8_t>(c)});
        }
    };
    if (s.next_board >= 0 && !board_closed(s, s.next_board)) add_board(s.next_board);
    else for (int b = 0; b < 9; ++b) add_board(b);
    return out;
}

bool Game::is_legal(const State& s, Move m) const {
    if (m.board >= 9 || m.cell >= 9 || outcome(s) != Outcome::InPlay) return false;
    if (board_closed(s, m.board)) return false;
    if ((s.x[m.board] | s.o[m.board]) & (1u << m.cell)) return false;
    if (s.next_board >= 0 && !board_closed(s, s.next_board) && m.board != s.next_board) return false;
    return true;
}

State Game::play(const State& s, Move m) const {
    if (!is_legal(s, m)) throw std::invalid_argument("illegal UTTT move: " + move_to_string(m));
    State n = s;
    if (s.turn == Player::X) n.x[m.board] |= static_cast<uint16_t>(1u << m.cell);
    else n.o[m.board] |= static_cast<uint16_t>(1u << m.cell);
    n.turn = other(s.turn);
    n.next_board = board_closed(n, m.cell) ? -1 : static_cast<int8_t>(m.cell);
    return n;
}

Key Game::pack(const std::array<uint16_t,9>& x,
               const std::array<uint16_t,9>& o,
               int next_board, Player turn) const {
    Key k{};
    unsigned pos = 0;
    auto put = [&](uint64_t value, unsigned width) {
        unsigned idx = pos / 64;
        unsigned off = pos % 64;
        k.w[idx] |= value << off;
        if (off + width > 64) k.w[idx+1] |= value >> (64 - off);
        pos += width;
    };
    for (int b = 0; b < 9; ++b) put(x[b] & 0x1FFu, 9);
    for (int b = 0; b < 9; ++b) put(o[b] & 0x1FFu, 9);
    put(static_cast<uint64_t>(next_board < 0 ? 15 : next_board), 4);
    put(static_cast<uint64_t>(turn == Player::O), 1);
    return k;
}

Key Game::raw_key(const State& s) const {
    int nb = s.next_board;
    if (nb >= 0 && board_closed(s, nb)) nb = -1;
    return pack(s.x, s.o, nb, s.turn);
}

Key Game::canonical_key(const State& s) const {
    Key best{};
    bool first = true;
    for (int g = 0; g < 8; ++g) {
        std::array<uint16_t,9> tx{}, to{};
        for (int b = 0; b < 9; ++b) {
            int nb = sym_map_[g][b];
            tx[nb] = mask_xform_[g][s.x[b] & 0x1FFu];
            to[nb] = mask_xform_[g][s.o[b] & 0x1FFu];
        }
        int next = s.next_board < 0 ? -1 : sym_map_[g][s.next_board];
        if (next >= 0 && (has_line(tx[next]) || has_line(to[next]) || ((tx[next] | to[next]) == 0x1FFu))) next = -1;
        Key cur = pack(tx, to, next, s.turn);
        if (first || cur < best) { best = cur; first = false; }
    }
    return best;
}

std::string Game::move_to_string(Move m) const {
    std::string s;
    s.push_back(static_cast<char>('a' + m.board));
    s.push_back(static_cast<char>('a' + m.cell));
    return s;
}

std::optional<Move> Game::parse_move(const std::string& text) const {
    if (text.size() != 2) return std::nullopt;
    char a = static_cast<char>(std::tolower(static_cast<unsigned char>(text[0])));
    char b = static_cast<char>(std::tolower(static_cast<unsigned char>(text[1])));
    if (a < 'a' || a > 'i' || b < 'a' || b > 'i') return std::nullopt;
    return Move{static_cast<uint8_t>(a-'a'), static_cast<uint8_t>(b-'a')};
}

std::string Game::render(const State& s) const {
    std::ostringstream os;
    for (int big_r = 0; big_r < 3; ++big_r) {
        for (int r = 0; r < 3; ++r) {
            for (int big_c = 0; big_c < 3; ++big_c) {
                int b = 3*big_r + big_c;
                for (int c = 0; c < 3; ++c) {
                    int cell = 3*r + c;
                    char ch = '.';
                    if (s.x[b] & (1u << cell)) ch = 'X';
                    else if (s.o[b] & (1u << cell)) ch = 'O';
                    os << ch;
                }
                if (big_c != 2) os << " | ";
            }
            os << '\n';
        }
        if (big_r != 2) os << "------+-------+------\n";
    }
    os << "turn=" << player_char(s.turn) << " next=";
    if (s.next_board < 0 || board_closed(s, s.next_board)) os << "free";
    else os << static_cast<char>('a' + s.next_board);
    os << '\n';
    return os.str();
}

} // namespace uttt
