#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <memory>
#include <string>
#include <thread>

#include <curl/curl.h>

#include "../core/grid.h"
#include "../core/wave_engine.h"
#include "../data/live_feed_client.h"
#include "../data/mock_buoy_feed.h"
#include "terminal_renderer.h"

namespace {

std::atomic<bool> gRunning{true};
void handleSigint(int) { gRunning = false; }

double nowSeconds() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// Blocking GET, good enough for a preview polling every tens of seconds.
bool httpGet(const std::string& url, std::string& outBody) {
    CURL* curl = curl_easy_init();
    if (!curl) return false;

    outBody.clear();
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &outBody);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "living-wave-artwork-preview/1.0");

    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    curl_easy_cleanup(curl);

    return res == CURLE_OK && httpCode >= 200 && httpCode < 300;
}

} // namespace

int main(int argc, char** argv) {
    bool statsMode = false;
    double mockInterval = 45.0;
    std::string liveUrl;
    double liveIntervalSeconds = 60.0;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--stats") == 0) {
            statsMode = true;
        } else if (std::strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            mockInterval = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--live-url") == 0 && i + 1 < argc) {
            liveUrl = argv[++i];
        } else if (std::strcmp(argv[i], "--live-poll-interval") == 0 && i + 1 < argc) {
            liveIntervalSeconds = std::atof(argv[++i]);
        }
    }

    std::signal(SIGINT, handleSigint);

    const bool liveMode = !liveUrl.empty();
    // Expected-update-interval drives staleness/confidence decay -- match it
    // to whichever cadence is actually in play so that mechanism means the
    // same thing in both modes.
    wave::WaveEngine engine(liveMode ? liveIntervalSeconds : mockInterval);
    wave::MockBuoyFeed feed(mockInterval); // unused in live mode, harmless
    wave::Grid<wave::RGB8> frame;

    if (liveMode) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    std::unique_ptr<wave::TerminalRenderer> renderer;
    if (!statsMode) {
        if (liveMode) {
            std::printf(
                "Living Wave Artwork -- terminal preview (live: %s, polling every %.0fs).\n"
                "Widen your terminal to ~130 columns. Ctrl+C to quit.\n\n",
                liveUrl.c_str(), liveIntervalSeconds);
        } else {
            std::printf(
                "Living Wave Artwork -- terminal preview (mock feed, %.0fs cadence).\n"
                "Widen your terminal to ~130 columns. Ctrl+C to quit.\n\n",
                mockInterval);
        }
        renderer = std::make_unique<wave::TerminalRenderer>();
    }

    const double frameInterval = 1.0 / 20.0;
    double t0 = nowSeconds();
    double nextFrameAt = 0.0;
    double nextLivePollAt = 0.0;

    while (gRunning) {
        double t = nowSeconds() - t0;

        if (liveMode) {
            if (t >= nextLivePollAt) {
                nextLivePollAt = t + liveIntervalSeconds;
                std::string body;
                if (httpGet(liveUrl, body)) {
                    wave::BuoyReading reading = wave::LiveFeedClient::parsePayload(body);
                    if (reading.valid) {
                        engine.ingest(reading, t);
                    } else if (statsMode) {
                        std::printf("t=%6.1fs  live fetch OK but payload failed to parse\n", t);
                    }
                } else if (statsMode) {
                    std::printf("t=%6.1fs  live fetch failed (network/HTTP error)\n", t);
                }
            }
        } else {
            wave::BuoyReading reading;
            if (feed.poll(t, reading)) {
                engine.ingest(reading, t);
            }
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
                    std::snprintf(status, sizeof(status),
                                  liveMode ? "waiting for first live fetch..." : "waiting for first mock reading...");
                }
                renderer->draw(frame, status);
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }

    if (liveMode) {
        curl_global_cleanup();
    }

    if (!statsMode) {
        std::printf("\nExiting.\n");
    }
    return 0;
}
