d := $(dir $(lastword $(MAKEFILE_LIST)))

SRCS += $(addprefix $(d), validation_parse_client.cc estimate_policy.cc endorsement_client.cc)

LIB-common-sintring := $(LIB-store-common) $(OBJS-all-bench-clients) $(LIB-policy) \
	$(o)validation_parse_client.o $(o)estimate_policy.o $(o)endorsement_client.o
