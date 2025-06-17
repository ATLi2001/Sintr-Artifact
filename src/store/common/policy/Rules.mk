d := $(dir $(lastword $(MAKEFILE_LIST)))

SRCS += $(addprefix $(d), policy_client.cc policy_parse_client.cc weight_policy.cc and_policy.cc \
		or_policy.cc uniform_client_selector.cc zipf_client_selector.cc)

PROTOS += $(addprefix $(d), policy-proto.proto)

LIB-policy := $(LIB-store-common) $(o)policy_client.o $(o)policy-proto.o \
	$(o)policy_parse_client.o $(o)weight_policy.o $(o)and_policy.o $(o)or_policy.o \
	 $(o)uniform_client_selector.o $(o)zipf_client_selector.o
