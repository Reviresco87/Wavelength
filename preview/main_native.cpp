#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <csignal>
#include <ctime>
#include <fstream>
#include <memory>
#include <sstream>
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

int64_t realWallClockUnixSeconds() {
    using namespace std::chrono;
    return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
}

// Parses "YYYY-MM-DDTHH:MM:SS" (a trailing 'Z' is tolerated, not required)
// as UTC. Returns -1 on failure so the caller can fall back to real time
// rather than silently running with a garbage clock.
int64_t parseIso8601Utc(const char* s) {
    std::tm tm{};
    if (strptime(s, "%Y-%m-%dT%H:%M:%S", &tm) == nullptr) return -1;
    return static_cast<int64_t>(timegm(&tm));
}

size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// Local scenario files are read on a short, fixed poll interval (not exposed
// as a flag -- disk reads are cheap, unlike --live-url's network calls) so
// editing a scenario file updates the running preview without a restart.
constexpr double kScenarioPollIntervalSeconds = 2.0;

bool readFile(const std::string& path, std::string& outBody) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss;
    ss << f.rdbuf();
    outBody = ss.str();
    return true;
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
    std::string scenarioPath;
    int64_t simTimeStartUnix = -1;
    bool cloudCoverOverridePresent = false;
    float cloudCoverOverridePercent = 0.0f;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--stats") == 0) {
            statsMode = true;
        } else if (std::strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            mockInterval = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--live-url") == 0 && i + 1 < argc) {
            liveUrl = argv[++i];
        } else if (std::strcmp(argv[i], "--live-poll-interval") == 0 && i + 1 < argc) {
            liveIntervalSeconds = std::atof(argv[++i]);
        } else if (std::strcmp(argv[i], "--scenario") == 0 && i + 1 < argc) {
            scenarioPath = argv[++i];
        } else if (std::strcmp(argv[i], "--sim-time") == 0 && i + 1 < argc) {
            simTimeStartUnix = parseIso8601Utc(argv[++i]);
            if (simTimeStartUnix < 0) {
                std::fprintf(stderr, "--sim-time: couldn't parse '%s', expected YYYY-MM-DDTHH:MM:SSZ\n", argv[i]);
                return 1;
            }
        } else if (std::strcmp(argv[i], "--cloud-cover") == 0 && i + 1 < argc) {
            cloudCoverOverridePresent = true;
            cloudCoverOverridePercent = static_cast<float>(std::atof(argv[++i]));
        }
    }

    if (!liveUrl.empty() && !scenarioPath.empty()) {
        std::fprintf(stderr, "--live-url and --scenario are mutually exclusive -- pick one feed.\n");
        return 1;
    }

    std::signal(SIGINT, handleSigint);

    const bool liveMode = !liveUrl.empty();
    const bool scenarioMode = !scenarioPath.empty();
    // Expected-update-interval drives staleness/confidence decay -- match it
    // to whichever cadence is actually in play so that mechanism means the
    // same thing across modes. A scenario file is re-read often enough
    // (kScenarioPollIntervalSeconds) that it never reads as stale either.
    wave::WaveEngine engine((liveMode || scenarioMode) ? liveIntervalSeconds : mockInterval);
    wave::MockBuoyFeed feed(mockInterval); // unused outside mock mode, harmless
    wave::Grid<wave::RGB8> frame;

    if (liveMode) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    // sim-time offset is constant, applied to the real clock, so time still
    // flows normally during the session -- just starting from wherever
    // --sim-time asked for, rather than a clock frozen at one instant.
    int64_t wallClockOffsetSeconds = (simTimeStartUnix >= 0) ? (simTimeStartUnix - realWallClockUnixSeconds()) : 0;

    std::unique_ptr<wave::TerminalRenderer> renderer;
    if (!statsMode) {
        if (liveMode) {
            std::printf(
                "Living Wave Artwork -- terminal preview (live: %s, polling every %.0fs).\n"
                "Widen your terminal to ~130 columns. Ctrl+C to quit.\n\n",
                liveUrl.c_str(), liveIntervalSeconds);
        } else if (scenarioMode) {
            std::printf(
                "Living Wave Artwork -- terminal preview (scenario: %s, re-read every %.0fs).\n"
                "Widen your terminal to ~130 columns. Ctrl+C to quit.\n\n",
                scenarioPath.c_str(), kScenarioPollIntervalSeconds);
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
    double nextScenarioPollAt = 0.0;

    while (gRunning) {
        double t = nowSeconds() - t0;
        int64_t wallClockUnixSeconds = realWallClockUnixSeconds() + wallClockOffsetSeconds;

        if (liveMode) {
            if (t >= nextLivePollAt) {
                nextLivePollAt = t + liveIntervalSeconds;
                std::string body;
                if (httpGet(liveUrl, body)) {
                    wave::BuoyReading reading = wave::LiveFeedClient::parsePayload(body);
                    if (reading.valid) {
                        if (cloudCoverOverridePresent) {
                            reading.cloudCoverPercent = cloudCoverOverridePercent;
                            reading.cloudCoverPresent = true;
                        }
                        engine.ingest(reading, t);
                    } else if (statsMode) {
                        std::printf("t=%6.1fs  live fetch OK but payload failed to parse\n", t);
                    }
                } else if (statsMode) {
                    std::printf("t=%6.1fs  live fetch failed (network/HTTP error)\n", t);
                }
            }
        } else if (scenarioMode) {
            if (t >= nextScenarioPollAt) {
                nextScenarioPollAt = t + kScenarioPollIntervalSeconds;
                std::string body;
                if (readFile(scenarioPath, body)) {
                    wave::BuoyReading reading = wave::LiveFeedClient::parsePayload(body);
                    if (reading.valid) {
                        if (cloudCoverOverridePresent) {
                            reading.cloudCoverPercent = cloudCoverOverridePercent;
                            reading.cloudCoverPresent = true;
                        }
                        engine.ingest(reading, t);
                    } else if (statsMode) {
                        std::printf("t=%6.1fs  scenario file read OK but payload failed to parse\n", t);
                    }
                } else if (statsMode) {
                    std::printf("t=%6.1fs  scenario file read failed: %s\n", t, scenarioPath.c_str());
                }
            }
        } else {
            wave::BuoyReading reading;
            if (feed.poll(t, reading)) {
                if (cloudCoverOverridePresent) {
                    reading.cloudCoverPercent = cloudCoverOverridePercent;
                    reading.cloudCoverPresent = true;
                }
                engine.ingest(reading, t);
            }
        }
        engine.tick(t, wallClockUnixSeconds);

        if (t >= nextFrameAt) {
            nextFrameAt = t + frameInterval;

            if (statsMode) {
                const wave::BuoyReading& s = engine.currentState();
                const wave::SkyState& sky = engine.currentSky();
                std::printf(
                    "t=%6.1fs  hs=%.2fm  tp=%.2fs  mwd=%6.1f  confidence=%.2f  sinceUpdate=%.1fs  |  "
                    "sunElev=%6.1f  warmth=%.2f  glint=%.2f  brightness=%.2f\n",
                    t, s.hsMetres, s.tpSeconds, s.mwdDeg, engine.confidence(), engine.secondsSinceLastUpdate(t),
                    sky.sunElevationDeg, sky.warmth, sky.glintScale, sky.brightnessScale);
            } else {
                engine.renderFrame(frame);
                char status[200];
                const wave::SkyState& sky = engine.currentSky();
                if (engine.hasData()) {
                    const wave::BuoyReading& s = engine.currentState();
                    std::snprintf(status, sizeof(status),
                                  "Hs %.2fm  Tp %.2fs  MWD %.0f\xC2\xB0  confidence %.0f%%  |  sun %.0f\xC2\xB0",
                                  s.hsMetres, s.tpSeconds, s.mwdDeg, engine.confidence() * 100.0f,
                                  sky.sunElevationDeg);
                } else {
                    const char* waitingLabel = liveMode      ? "waiting for first live fetch..."
                                                : scenarioMode ? "waiting for first scenario read..."
                                                               : "waiting for first mock reading...";
                    std::snprintf(status, sizeof(status), "%s  |  sun %.0f\xC2\xB0", waitingLabel, sky.sunElevationDeg);
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
