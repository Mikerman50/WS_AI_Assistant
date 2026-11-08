# Host builds of the portable core (no ESP-IDF needed).
#   make test      run the unit tests
#   make preview   render the face states to build/*.pbm
#   make tts       speak sample sentences with the on-device voice into build/voice_*.wav
#   make embed     build the voice component through its CMakeLists.txt (needs cmake)
#   make speaker   run main/speaker.c with the real voice on stand-ins for FreeRTOS and the hardware

CC ?= cc
CORE_DIR := ../../main/core
CJSON_DIR := third_party/cJSON
CFLAGS := -std=c11 -Wall -Wextra -Werror -g -O1 -fsanitize=address,undefined -I$(CORE_DIR) -I$(CJSON_DIR)
CORE := $(wildcard $(CORE_DIR)/*.c) $(wildcard $(CORE_DIR)/fonts/*.c)

PICO_DIR := ../../components/picotts
PICO_LIB := $(filter-out $(PICO_DIR)/pico/lib/picorsrc.c,$(wildcard $(PICO_DIR)/pico/lib/*.c))
# The engine is third-party C: build it as the firmware does, without the strict warnings.
PICO_CFLAGS := -std=gnu11 -O2 -w -I$(PICO_DIR)/pico/lib -I$(PICO_DIR)/include -I$(PICO_DIR)

all: build/tests build/preview build/tts_sample build/speaker_sim

build:
	mkdir -p build

# cJSON is the same library ESP-IDF ships as its "json" component; built without -Werror.
build/cJSON.o: $(CJSON_DIR)/cJSON.c | build
	$(CC) -std=c11 -g -O1 -fsanitize=address,undefined -c -o $@ $<

build/tests: test_main.c $(CORE) build/cJSON.o | build
	$(CC) $(CFLAGS) -o $@ test_main.c $(CORE) build/cJSON.o -lm

build/preview: preview.c $(CORE) build/cJSON.o | build
	$(CC) $(CFLAGS) -o $@ preview.c $(CORE) build/cJSON.o -lm

build/pico.a: $(PICO_LIB) $(PICO_DIR)/esp_picorsrc.c | build
	rm -rf build/pico && mkdir -p build/pico
	for f in $(PICO_LIB); do \
		extra=""; case $$f in */picoos.c) extra="-Dpicoos_quick_exp=picoos_quick_nope";; esac; \
		$(CC) $(PICO_CFLAGS) $$extra -c -o build/pico/$$(basename $$f .c).o $$f || exit 1; \
	done
	$(CC) $(PICO_CFLAGS) -c -o build/pico/esp_picorsrc.o $(PICO_DIR)/esp_picorsrc.c
	ar rcs $@ build/pico/*.o

build/tts_sample: tts_sample.c $(PICO_DIR)/picotts_direct.c $(CORE_DIR)/tts_text.c $(CORE_DIR)/utf8.c build/pico.a | build
	$(CC) -std=gnu11 -Wall -Wextra -Werror -g -O1 -I$(CORE_DIR) -I$(PICO_DIR)/include -I$(PICO_DIR)/pico/lib -I$(PICO_DIR) \
		-o $@ tts_sample.c $(PICO_DIR)/picotts_direct.c $(CORE_DIR)/tts_text.c $(CORE_DIR)/utf8.c build/pico.a -lm

test: build/tests
	./build/tests

tts: build/tts_sample
	./build/tts_sample build

preview: build/preview
	./build/preview build

# main/speaker.c itself, on stand-ins for FreeRTOS, the codec and the screen.
SIM_CFLAGS := -std=gnu11 -Wall -Wextra -Werror -g -O1 -pthread -fsanitize=undefined -fno-sanitize-recover=undefined \
	-DESP_PLATFORM -Ibuild/idf_inc -I../idf_stub -I../../main -I$(CORE_DIR) -I$(CJSON_DIR) \
	-I$(PICO_DIR)/include -I$(PICO_DIR)/pico/lib -I$(PICO_DIR)
build/cJSON_plain.o: $(CJSON_DIR)/cJSON.c | build
	$(CC) -std=c11 -g -O1 -c -o $@ $<

build/speaker_sim: speaker_sim.c ../../main/speaker.c $(CORE) $(PICO_DIR)/picotts_direct.c build/pico.a build/cJSON_plain.o | build
	bash ../idf_stub/gen_headers.sh build/idf_inc
	$(CC) $(SIM_CFLAGS) -o $@ speaker_sim.c ../../main/speaker.c $(CORE) $(PICO_DIR)/picotts_direct.c \
		build/pico.a build/cJSON_plain.o -lm

VOICE_FILES := $(PICO_DIR)/pico/lang/en-GB_ta.bin $(PICO_DIR)/pico/lang/en-GB_kh0_sg.bin
speaker: build/speaker_sim
	./build/speaker_sim voice $(VOICE_FILES)
	./build/speaker_sim novoice $(VOICE_FILES)

embed:
	cmake -S voice_embed -B build/voice_embed >/dev/null
	cmake --build build/voice_embed
	./build/voice_embed/voice_embed

clean:
	rm -rf build

.PHONY: all test preview tts embed speaker clean
