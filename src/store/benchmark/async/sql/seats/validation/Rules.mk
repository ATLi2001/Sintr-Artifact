d := $(dir $(lastword $(MAKEFILE_LIST)))

SRCS += $(addprefix $(d), seats_transaction.cc delete_reservation.cc find_flights.cc find_open_seats.cc new_reservation.cc update_customer.cc update_reservation.cc)

OBJ-validation-sql-seats-transaction := $(LIB-store-frontend) $(o)seats_transaction.o

LIB-validation-sql-seats := $(OBJ-validation-sql-seats-transaction) $(LIB-sql-seats) $(o)delete_reservation.o \
	$(o)find_flights.o $(o)find_open_seats.o $(o)new_reservation.o $(o)update_customer.o $(o)update_reservation.o
