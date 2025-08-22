d := $(dir $(lastword $(MAKEFILE_LIST)))

SRCS += $(addprefix $(d), autobahn_agent.cc)

LIB-autobahn := $(o)autobahn_agent.o
