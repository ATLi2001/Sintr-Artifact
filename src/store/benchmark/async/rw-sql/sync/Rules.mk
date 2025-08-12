d := $(dir $(lastword $(MAKEFILE_LIST)))

SRCS += $(addprefix $(d), rw-sql_client.cc rw-sql_transaction.cc rw-sql_policy_change.cc)

OBJ-rw-sql-transaction := $(LIB-store-frontend) $(o)rw-sql_transaction.o $(o)rw-sql_policy_change.o

OBJ-rw-sql-client := $(o)rw-sql_client.o

LIB-rw-sql := $(OBJ-rw-sql-client) $(OBJ-rw-sql-transaction) 
