.PHONY: run clean build #deb build-tests test check-style clang-format changelog

NPROCS := 1
OS := $(shell uname)
export NPROCS

ifeq ($(OS),Linux)
	NPROCS := $(shell grep -c ^processor /proc/cpuinfo)
else ifeq ($(OS),Darwin)
	NPROCS := $(shell sysctl -n hw.ncpu)
endif # $(OS)

all: build

build/Makefile:
	mkdir -p build
	cd build && cmake -DOPT_BUILD_TESTS=1 ..

init: build/Makefile

build: init
	cd build && make -j$(NPROCS)

clean:
	rm -fr build

#run: build
#	/usr/sbin/fastcgi-daemon2 --config=manual/fastcgi.conf

#build-tests: init
#	cd build && make -j$(NPROCS) auto-tests

#test: build-tests
#	cd build && ./tests/auto-tests

#clang-format:
#	find src/ -type f \! -path "*third_party*" | xargs clang-format -i

#check-style:
#	./tools/cpplint.py --extensions=hpp,cpp  --counting=detailed \
		`find src/ -type f \! -path "*third_party*"`

#changelog:
#	@../tools/build_changelog.sh


#deb:
#	DEB_BUILD_OPTIONS="parallel=$(NPROCS)" debuild -b


