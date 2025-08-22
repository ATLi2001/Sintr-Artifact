d := $(dir $(lastword $(MAKEFILE_LIST)))

SRCS += $(addprefix $(d), autobahn_agent.cc library/bftinterface/cppinclude/autobahn_callback.cc)

LIB-autobahn := $(o)autobahn_agent.o $(o)library/bftinterface/cppinclude/autobahn_callback.o
