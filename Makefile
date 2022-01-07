# Copyright 2018 Oticon A/S
# SPDX-License-Identifier: Apache-2.0

BSIM_BASE_PATH?=$(abspath ../ )
include ${BSIM_BASE_PATH}/common/pre.make.inc

2G4_libPhyComv1_COMP_PATH?=$(abspath ${BSIM_COMPONENTS_PATH}/ext_2G4_libPhyComv1)
2G4_phy_v1_COMP_PATH?=$(abspath ${BSIM_COMPONENTS_PATH}/ext_2G4_phy_v1)

SRCS:=src/channel_positional.c \
      src/channel_positional_args.c

INCLUDES:= -I${libUtilv1_COMP_PATH}/src/ \
           -I${libPhyComv1_COMP_PATH}/src/ \
           -I${2G4_phy_v1_COMP_PATH}/src/ \
           -I${2G4_libPhyComv1_COMP_PATH}/src

LIB_NAME:=lib_2G4Channel_positional
A_LIBS:=
#remember to compile whoever uses this with -rdynamic so we can use its libUtils functions
SO_LIBS:=

DEBUG:=-g
OPT:=
ARCH:=
WARNINGS:=-Wall -pedantic
COVERAGE:=
CFLAGS:=${ARCH} ${DEBUG} ${OPT} ${WARNINGS} -MMD -MP -std=c99  -fPIC ${INCLUDES}
LDFLAGS:=${ARCH} ${COVERAGE}
CPPFLAGS:=

include ${BSIM_BASE_PATH}/common/make.lib_so.inc
