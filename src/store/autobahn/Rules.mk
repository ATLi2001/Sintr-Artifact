d := $(dir $(lastword $(MAKEFILE_LIST)))

SRCS += $(addprefix $(d), autobahn_agent.cc library/bftinterface/cppinclude/autobahn_callback.cc library/bridge_debug/cppinclude/autobahn_debug.cc)

LIB-autobahn := $(o)autobahn_agent.o $(o)library/bftinterface/cppinclude/autobahn_callback.o $(o)library/bridge_debug/cppinclude/autobahn_debug.o
