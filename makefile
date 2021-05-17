TITLE_COLOR = \033[33m
NO_COLOR = \033[0m
FLAGS = -Wall -std=c11 -Werror -fdiagnostics-color=auto
DEBUG_LVL = 1
GATEWAY_CONFIG = -DSET_MIN_TEMP=10 -DSET_MAX_TEMP=20 -DTIMEOUT=5 -DMAX_CONN=10 -DDEBUG_LVL=$(DEBUG_LVL)
NODE_CONFIG = -DLOOPS=3600 -DLOG_SENSOR_DATA
PORT = 1234

# when executing make, compile all exe's
all: clean-all all_libs sensor_gateway sensor_node file_creator

# When trying to compile one of the executables, first look for its .c files
# Then check if the libraries are in the lib folder
sensor_gateway: main.c sbuffer.c connmgr.c datamgr.c sensor_db.c lib/libdplist.so lib/libtcpsock.so
	@echo "$(TITLE_COLOR)\n***** CPPCHECK *****$(NO_COLOR)"
	cppcheck --enable=all --suppress=missingIncludeSystem main.c connmgr.c datamgr.c sensor_db.c sbuffer.c
	@echo "$(TITLE_COLOR)\n***** COMPILING sensor_gateway *****$(NO_COLOR)"
	gcc -c -g main.c      $(GATEWAY_CONFIG) -o main.o      $(FLAGS)
	gcc -c -g sbuffer.c   $(GATEWAY_CONFIG) -o sbuffer.o   $(FLAGS)
	gcc -c -g connmgr.c   $(GATEWAY_CONFIG) -o connmgr.o   $(FLAGS)
	gcc -c -g datamgr.c   $(GATEWAY_CONFIG) -o datamgr.o   $(FLAGS)
	gcc -c -g sensor_db.c $(GATEWAY_CONFIG) -o sensor_db.o $(FLAGS)
	@echo "$(TITLE_COLOR)\n***** LINKING sensor_gateway *****$(NO_COLOR)"
	gcc -g main.o sbuffer.o connmgr.o datamgr.o sensor_db.o -ldplist -ltcpsock -lsqlite3 -lpthread -o sensor_gateway -Wall -L./lib -Wl,-rpath=./lib -fdiagnostics-color=auto

file_creator: file_creator.c
	@echo "$(TITLE_COLOR)\n***** COMPILE & LINKING file_creator *****$(NO_COLOR)"
	gcc -DDEBUG file_creator.c -o file_creator -Wall -fdiagnostics-color=auto

sensor_node: sensor_node.c lib/libtcpsock.so
	@echo "$(TITLE_COLOR)\n***** COMPILING sensor_node *****$(NO_COLOR)"
	gcc -c -g sensor_node.c $(NODE_CONFIG) -o sensor_node.o $(FLAGS)
	@echo "$(TITLE_COLOR)\n***** LINKING sensor_node *****$(NO_COLOR)"
	gcc sensor_node.o -ltcpsock -o sensor_node -Wall -L./lib -Wl,-rpath=./lib -fdiagnostics-color=auto

all_libs: libdplist libtcpsock

# If you only want to compile one of the libs, this target will match (e.g. make liblist)
libdplist: lib/libdplist.so
libtcpsock: lib/libtcpsock.so

lib/libdplist.so: lib/dplist.c
	@echo "$(TITLE_COLOR)\n***** COMPILING LIB dplist *****$(NO_COLOR)"
	gcc -c lib/dplist.c -fPIC -o lib/dplist.o $(FLAGS)
	@echo "$(TITLE_COLOR)\n***** LINKING LIB dplist< *****$(NO_COLOR)"
	gcc lib/dplist.o -o lib/libdplist.so -Wall -shared -lm -fdiagnostics-color=auto

lib/libtcpsock.so: lib/tcpsock.c
	@echo "$(TITLE_COLOR)\n***** COMPILING LIB tcpsock *****$(NO_COLOR)"
	gcc -c lib/tcpsock.c -fPIC -o lib/tcpsock.o $(FLAGS)
	@echo "$(TITLE_COLOR)\n***** LINKING LIB tcpsock *****$(NO_COLOR)"
	gcc lib/tcpsock.o -o lib/libtcpsock.so -Wall -shared -lm -fdiagnostics-color=auto

# do not look for files called clean, clean-all or this will be always a target
.PHONY: clean clean-all 

clean:
	rm -rf sensor_log* *.png *.html ./coverage/*

clean-all: clean
	rm -rf *.o lib/*.o lib/*.so sensor_gateway sensor_node file_creator *~ 

leak: all
	@echo "$(TITLE_COLOR)\n***** LEAK CHECK sensor_gateway *****$(NO_COLOR)"
	valgrind --leak-check=full -v --track-origins=yes --show-leak-kinds=all ./sensor_gateway $(PORT)

run:
	@echo "$(TITLE_COLOR)\n***** RUNNING sensor_gateway *****$(NO_COLOR)"
	./sensor_gateway $(PORT)

test: main.c sbuffer.c connmgr.c datamgr.c sensor_db.c lib/libdplist.so lib/libtcpsock.so
	@echo "$(TITLE_COLOR)\n***** CPPCHECK *****$(NO_COLOR)"
	cppcheck --enable=all --suppress=missingIncludeSystem main.c connmgr.c datamgr.c sensor_db.c sbuffer.c
	@echo "$(TITLE_COLOR)\n***** COMPILING sensor_gateway *****$(NO_COLOR)"
	gcc -c -g main.c      $(GATEWAY_CONFIG) -o main.o      --coverage $(FLAGS)
	gcc -c -g sbuffer.c   $(GATEWAY_CONFIG) -o sbuffer.o   --coverage $(FLAGS)
	gcc -c -g connmgr.c   $(GATEWAY_CONFIG) -o connmgr.o   --coverage $(FLAGS)
	gcc -c -g datamgr.c   $(GATEWAY_CONFIG) -o datamgr.o   --coverage $(FLAGS)
	gcc -c -g sensor_db.c $(GATEWAY_CONFIG) -o sensor_db.o --coverage $(FLAGS)
	@echo "$(TITLE_COLOR)\n***** LINKING sensor_gateway *****$(NO_COLOR)"
	gcc --coverage main.o sbuffer.o connmgr.o datamgr.o sensor_db.o -ldplist -ltcpsock -lsqlite3 -lpthread -o sensor_gateway -Wall -L./lib -Wl,-rpath=./lib -fdiagnostics-color=auto

clean-coverage:
	@echo -e '\n*********************************'
	@echo -e '*** Cleanup of coverage files ***'
	@echo -e '*********************************'
	rm -f ./*.gcda ./*.gcno

coverage: clean-all clean-coverage all_libs test sensor_node
	./sensor_gateway 1234
	lcov --capture --directory . --output-file ./coverage/coverage.info
	genhtml ./coverage/coverage.info --output-directory ./coverage
	make clean-coverage

show-coverage: coverage
	firefox ./coverage/index.html