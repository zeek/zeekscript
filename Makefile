all: build test

build:
	python setup.py build

test:
	python ./tests/test_all.py
