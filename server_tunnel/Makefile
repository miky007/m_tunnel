COMPILER         = -g++
OPTIMIZATION_OPT = -O2
OPTIONS          = -pedantic -ansi -Wall -Werror $(OPTIMIZATION_OPT) -o
PTHREAD          = -lpthread
LINKER_OPT       = -L/usr/lib -lstdc++ -std=c++11 $(PTHREAD) -lboost_thread -lboost_system
BINNAME=stunel

BUILD_LIST+=${BINNAME}

all: $(BUILD_LIST)

${BINNAME}: stunnel.cpp
	$(COMPILER) $(OPTIONS) ${BINNAME} stunnel.cpp $(LINKER_OPT)

strip_bin :
	strip -s ${BINNAME}

clean:
	rm stunel *.o
