#include "terminal_renderer.h"

#include <cstdio>
#include <string>

namespace wave {

TerminalRenderer::TerminalRenderer() {
    std::printf("\x1b[?25l"); // hide cursor
    std::fflush(stdout);
}

TerminalRenderer::~TerminalRenderer() {
    std::printf("\x1b[0m\x1b[?25h\n"); // reset colour, restore cursor
    std::fflush(stdout);
}

void TerminalRenderer::draw(const Grid<RGB8>& frame, const char* statusLine) {
    if (!firstDraw_) {
        std::printf("\x1b[H"); // cursor home: redraw in place, no flicker/scroll
    }
    firstDraw_ = false;

    std::string buf;
    buf.reserve(static_cast<size_t>(Grid<RGB8>::width()) * Grid<RGB8>::height() * 20);

    char cell[32];
    for (int y = 0; y < Grid<RGB8>::height(); ++y) {
        for (int x = 0; x < Grid<RGB8>::width(); ++x) {
            const RGB8& c = frame.at(x, y);
            // Two spaces per pixel approximates a square cell (terminal
            // characters are roughly twice as tall as they are wide).
            int n = std::snprintf(cell, sizeof(cell), "\x1b[48;2;%d;%d;%dm  ", c.r, c.g, c.b);
            buf.append(cell, static_cast<size_t>(n));
        }
        buf.append("\x1b[0m\n");
    }

    if (statusLine) {
        buf.append(statusLine);
        buf.append("\x1b[K\n"); // clear to end of line in case the status text shrinks
    }

    std::fwrite(buf.data(), 1, buf.size(), stdout);
    std::fflush(stdout);
}

} // namespace wave
