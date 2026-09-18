CC ?= gcc
CFLAGS ?= -std=c11 -Wall -Wextra -Wpedantic -Werror
CPPFLAGS ?= -Iinclude

BUILD_DIR := build
TEST_BIN := $(BUILD_DIR)/test_atm_domain

DOMAIN_SRC := src/atm_domain.c
STORAGE_SRC := src/atm_storage.c
TEST_SRC := tests/test_atm_domain.c

# uC/OS-II Win32 port location for the RTOS simulator.
UCOS_ROOT ?= C:/software/ucos-II
UCOS_SRC := $(UCOS_ROOT)/source
UCOS_PORT_SRC := $(UCOS_ROOT)/ports/80x86/win32/vc/src

.PHONY: all test check lint clean app-msvc clean-msvc

all: check test

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

check: $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $(DOMAIN_SRC) -o $(BUILD_DIR)/atm_domain.o
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $(STORAGE_SRC) -o $(BUILD_DIR)/atm_storage.o

test: $(BUILD_DIR)
	$(CC) $(CFLAGS) $(CPPFLAGS) $(TEST_SRC) $(DOMAIN_SRC) -o $(TEST_BIN)
	./$(TEST_BIN)

lint:
	cppcheck --enable=warning,style,performance,portability \
		--error-exitcode=1 --suppress=missingIncludeSystem \
		-Iinclude src/atm_domain.c src/atm_storage.c

# Run from a Visual Studio Developer Command Prompt with GNU Make.
# The external uC/OS-II source tree is intentionally not vendored.
app-msvc:
	cl -nologo /W4 /MD /Iinclude /I"$(UCOS_SRC)" /I"$(UCOS_PORT_SRC)" \
		src/atm_app.c src/atm_domain.c src/atm_storage.c \
		"$(UCOS_SRC)/ucos_ii.c" "$(UCOS_PORT_SRC)/pc.c" "$(UCOS_PORT_SRC)/os_cpu_c.c" \
		winmm.lib user32.lib /Fe:atm_refactored.exe

clean:
	rm -rf $(BUILD_DIR)

clean-msvc:
	@if exist *.obj del *.obj
	@if exist *.pdb del *.pdb
	@if exist *.ilk del *.ilk
	@if exist atm_refactored.exe del atm_refactored.exe
