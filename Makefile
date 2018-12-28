.PHONY: build debug all

BUILD_IMAGE ?= csuliuming/gko3-compile-env:v1.1

CWD := $(shell pwd)
CONTAINER_NAME := gko3

DOCKER_RUN_OPTS := \
	--name $(CONTAINER_NAME) \
        --rm \
	--hostname $(CONTAINER_NAME) \
	--volume=/etc/localtime:/etc/localtime:ro \
	--volume=$(CWD):$(CWD) \
	--workdir=$(CWD)

build:
	docker run $(DOCKER_RUN_OPTS) $(BUILD_IMAGE) bash -x ./build.sh

debug:
	docker run -it $(DOCKER_RUN_OPTS) $(BUILD_IMAGE) /bin/bash

all: build
