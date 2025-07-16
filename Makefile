CXX      := g++
CXXFLAGS := -std=c++17

# pull in the ffmpeg headers/libs only once
PKGFLAGS := $(shell pkg-config --cflags --libs libavformat libavcodec libavutil)

# list “1 2” → example1 example2
NUMS    := 1 2
EXES    := $(addprefix example,$(NUMS))

.PHONY: all clean
all: $(EXES) Makefile

# one rule for example1, example2, ... 
example%: example%.cpp Makefile
	$(CXX) $(CXXFLAGS) -o $@ $< $(PKGFLAGS)

clean:
	rm -f $(EXES) *.mp4
