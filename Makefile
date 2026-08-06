CXX ?= clang++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -MMD -MP

CORE_SRCS := core/noise.cpp core/palette.cpp core/wave_engine.cpp
DATA_SRCS := data/mock_buoy_feed.cpp data/cco_client.cpp
PREVIEW_SRCS := preview/main_native.cpp preview/terminal_renderer.cpp

SRCS := $(CORE_SRCS) $(DATA_SRCS) $(PREVIEW_SRCS)
OBJS := $(SRCS:.cpp=.o)
DEPS := $(OBJS:.o=.d)

BIN := preview_native

.PHONY: all clean

all: $(BIN)

$(BIN): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(DEPS)

clean:
	rm -f $(OBJS) $(DEPS) $(BIN)
