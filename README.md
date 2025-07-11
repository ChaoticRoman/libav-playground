# FFMPEG's libav* libraries playground

Initial example created with o4-mini, two small bugs fixed and it works. Need this to debug something...

## Dependencies

On Ubuntu run:

```
sudo apt install libavformat-dev libavcodec-dev
```

## Building

```
g++ -std=c++11 -o example1 example1.cpp  `pkg-config --cflags --libs libavformat libavcodec libavutil`
g++ -std=c++11 -o example2 example2.cpp  `pkg-config --cflags --libs libavformat libavcodec libavutil`
```
