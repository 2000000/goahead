# post limit 10M
AR=arm-linux-gnueabihf-ar CC=arm-linux-gnueabihf-gcc LD=arm-linux-gnueabihf-gcc NM=arm-linux-gnueabihf-nm  ./configure --platform linux-arm  --set goahead.limitPost=10485760
