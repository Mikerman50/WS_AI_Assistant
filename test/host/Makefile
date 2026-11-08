# Host builds of the portable core (no ESP-IDF needed).
#   make test      run the unit tests
#   make preview   render the face states to build/*.pbm

CC ?= cc
CORE_DIR := ../../main/core
CJSON_DIR := third_party/cJSON
CFLAGS := -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -I$(CORE_DIR) -I$(CJSON_DIR)
CORE := $(wildcard $(CORE_DIR)/*.c) $(wildcard $(CORE_DIR)/fonts/*.c)

all: build/tests build/preview

build:
	mkdir -p build

# cJSON is the same library ESP-IDF ships as its "json" component; built without -Werror.
build/cJSON.o: $(CJSON_DIR)/cJSON.c | build
	$(CC) -std=c11 -g -O1 -fsanitize=address,undefined -c -o $@ $<

build/tests: test_main.c $(CORE) build/cJSON.o | build
	$(CC) $(CFLAGS) -o $@ test_main.c $(CORE) build/cJSON.o -lm

build/preview: preview.c $(CORE) build/cJSON.o | build
	$(CC) $(CFLAGS) -o $@ preview.c $(CORE) build/cJSON.o -lm

test: build/tests
	./build/tests

preview: build/preview
	./build/preview build

clean:
	rm -rf build

.PHONY: all test preview clean
