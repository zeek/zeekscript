all: build test

build:
	python setup.py build

test:
	pytest
