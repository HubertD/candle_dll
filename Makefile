SRC = candle.c candle_ctrl_req.c
LIBS = -static-libgcc -lSetupApi -l winusb -lOle32
DEFS = -DCANDLE_API_LIBRARY -DUNICODE
CFLAGS = -std=gnu99 -shared

OUT = candle_api

all:
	mkdir -p build
	gcc $(CFLAGS) $(DEFS) $(SRC) $(LIBS) -o build/$(OUT).dll -Wl,--out-implib,build/$(OUT).lib

clean:
	rm build/$(OUT).dll build/$(OUT).lib build/$(OUT).h

