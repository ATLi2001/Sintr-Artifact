d := $(dir $(lastword $(MAKEFILE_LIST)))

SRCS += $(addprefix $(d), benchmark.cc benchmark_oneshot.cc bench_client.cc async_transaction_bench_client.cc sync_transaction_bench_client.cc)

OBJS-all-store-clients := $(OBJS-strong-client) $(OBJS-weak-client) \
		$(LIB-tapir-client) $(LIB-morty-client) $(LIB-janus-client) \
		$(LIB-indicus-client) $(LIB-pbft-store) $(LIB-hotstuff-client) $(LIB-hotstuff-pg-store) $(LIB-augustus-store) \
		$(LIB-bftsmart-client) $(LIB-bftsmart-augustus-store) $(LIB-bftsmart-stable-store) \
		$(LIB-pequin-client) $(LIB-postgres-client) $(LIB-blackhole-client) \
		$(LIB-cockroachdb-store) $(LIB-peloton-client) $(LIB-sintr-client)

LIB-bench-client := $(o)benchmark.o $(o)bench_client.o \
		$(o)async_transaction_bench_client.o $(o)sync_transaction_bench_client.o

OBJS-all-bench-clients := $(LIB-retwis) $(LIB-tpcc) $(LIB-sync-tpcc) $(LIB-async-tpcc) $(LIB-validation-tpcc) $(LIB-validation-sql-tpcc) \
	$(LIB-sync-sql-tpcc) $(LIB-sql-tpcc) $(LIB-smallbank) $(LIB-rw) $(LIB-toy) $(LIB-rw-sql) $(LIB-sql-seats) $(LIB-sql-tpcch) $(LIB-auctionmark) \
	$(LIB-rw-base) $(LIB-rw-sync) $(LIB-rw-val) $(LIB-rw-sql-val) $(LIB-rw-sql-base) $(LIB-sync-smallbank) $(LIB-validation-smallbank) \
	$(LIB-validation-sql-seats) $(LIB-sync-sql-seats) $(LIB-sql-tpcc-lifting) $(LIB-sync-sql-tpcc-lifting) $(LIB-validation-sql-tpcc-lifting)

$(d)benchmark: $(LIB-key-selector) $(LIB-bench-client) $(LIB-latency) $(LIB-tcptransport) $(LIB-udptransport) $(OBJS-all-store-clients) $(OBJS-all-bench-clients) $(LIB-bench-client) $(LIB-store-common)

# Add validation libs + sintring to store/server from here, where all sub-Rules.mk
# are already loaded so OBJS-all-bench-clients and LIB-common-sintring are fully defined.
store/server: $(OBJS-all-bench-clients) $(o)bench_client.o $(o)async_transaction_bench_client.o $(o)sync_transaction_bench_client.o $(LIB-common-sintring) $(LIB-store-frontend)

BINS += $(d)benchmark
