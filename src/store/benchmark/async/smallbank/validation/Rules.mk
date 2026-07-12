d := $(dir $(lastword $(MAKEFILE_LIST)))

SRCS += $(addprefix $(d), smallbank_transaction.cc amalgamate.cc bal.cc deposit.cc transact.cc write_check.cc)

OBJ-validation-smallbank-transaction := $(LIB-store-frontend) $(o)smallbank_transaction.o

LIB-validation-smallbank := $(OBJ-validation-smallbank-transaction) $(LIB-smallbank) $(o)amalgamate.o \
	$(o)deposit.o $(o)transact.o $(o)write_check.o $(o)bal.o
