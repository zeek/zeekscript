all: build test

build:
	python setup.py build

test:
	cd tests && python -m unittest
