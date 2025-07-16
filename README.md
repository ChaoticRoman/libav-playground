# FFMPEG's libav* libraries playground

Initial example created with o4-mini, two small bugs fixed and it works. Need this to debug something...

## Dependencies

On Ubuntu run:

```
sudo apt install make g++ libavformat-dev libavcodec-dev
```

## Building

```
make all
```

## Running

Non-fragmented MP4 encoding example is run with `./example1`.

Fragmented MP4 encoding example is run using `./example2`.

Output MP4 files are written to current directory.
