#!/bin/bash
#
# Copyright (C) 2018-2020 Intel Corporation
#
# SPDX-License-Identifier: MIT
#

git fetch -t
git clone ../compute-runtime neo
if [ -z "$DOCKER_USERNAME" ]
then
   docker login -u "$DOCKER_USERNAME" -p "$DOCKER_PASSWORD"
fi
docker info
docker build -f scripts/docker/Dockerfile-fedora-32-copr -t neo-fedora-32-copr:ci .

