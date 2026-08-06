#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <memory>
#include <thread>

#include "../core/grid.h"
#include "../core/wave_engine.h"
#include "../data/mock_buoy_feed.h"
#include "terminal_renderer.h"

namespace {

std::atomic<bool> gRunning{true};
void handleSigint(int) { gRunning = false; }

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

} // namespace

int main(int argc, char** argv) {
    bool statsMode = false;
    double mockInterval = 45.0;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--stats") == 0) {
            statsMode = true;
        } else if (std::strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            mockInterval = std::atof(argv[++i]);
        }
    }

    std::signal(SIGINT, handleSigint);

    // Expected-update-interval matches the mock cadence so staleness is
    // observable within a short session instead of a simulated ~75 minutes.
    wave::WaveEngine engine(mockInterval);
    wave::MockBuoyFeed feed(mockInterval);
    wave::Grid<wave::RGB8> frame;

    std::unique_ptr<wave::TerminalRenderer> renderer;
    if (!statsMode) {
        std::printf(
            "Living Wave Artwork -- terminal preview (mock feed, %.0fs cadence).\n"
            "Widen your terminal to ~130 columns. Ctrl+C to quit.\n\n",
            mockInterval);
        renderer = std::make_unique<wave::TerminalRenderer>();
    }

    const double frameInterval = 1.0 / 20.0;
    double t0 = nowSeconds();
    double nextFrameAt = 0.0;

    while (gRunning) {
        double t = nowSeconds() - t0;

        wave::BuoyReading reading;
        if (feed.poll(t, reading)) {
            engine.ingest(reading, t);
        }
        engine.tick(t);

        if (t >= nextFrameAt) {
            nextFrameAt = t + frameInterval;

            if (statsMode) {
                const wave::BuoyReading& s = engine.currentState();
                std::printf(
                    "t=%6.1fs  hs=%.2fm  tp=%.2fs  mwd=%6.1f  confidence=%.2f  sinceUpdate=%.1fs\n",
                    t, s.hsMetres, s.tpSeconds, s.mwdDeg, engine.confidence(),
                    engine.secondsSinceLastUpdate(t));
            } else {
                engine.renderFrame(frame);
                char status[160];
                if (engine.hasData()) {
                    const wave::BuoyReading& s = engine.currentState();
                    std::snprintf(status, sizeof(status),
                                  "Hs %.2fm  Tp %.2fs  MWD %.0f\xC2\xB0  confidence %.0f%%", s.hsMetres,
                                  s.tpSeconds, s.mwdDeg, engine.confidence() * 100.0f);
                } else {
                    std::snprintf(status, sizeof(status), "waiting for first mock reading...");
                }
                renderer->draw(frame, status);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (!statsMode) {
        std::printf("\nExiting.\n");
    }
    return 0;
}
