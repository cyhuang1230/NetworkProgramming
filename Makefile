CC=g++
CXXFLAGS= -std=gnu++11
DEBUG_FLAGS= -D DEBUG

hw1: 
	$(CC) NP_project1.cpp $(CXXFLAGS) -o np_hw1

hw1_debug:
	$(CC) NP_project1.cpp $(CXXFLAGS) $(DEBUG_FLAGS) -o np_hw1

clean:
	rm np_*