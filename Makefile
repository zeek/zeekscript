.PHONY: build test

all: test

build:
	python setup.py build

test: build
	cd tests && python -m unittest
