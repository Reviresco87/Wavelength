#pragma once

#include "../core/grid.h"

namespace wave {

// Renders a Grid<RGB8> as 24-bit ANSI colour blocks in the terminal,
// redrawing in place via cursor-home rather than scrolling/clearing.
class TerminalRenderer {
public:
    TerminalRenderer();
    ~TerminalRenderer();

    TerminalRenderer(const TerminalRenderer&) = delete;
    TerminalRenderer& operator=(const TerminalRenderer&) = delete;

    void draw(const Grid<RGB8>& frame, const char* statusLine);

private:
    bool firstDraw_ = true;
};

} // namespace wave
