CC=g++
CXXFLAGS= -std=c++11
DEBUG_FLAGS= -D DEBUG

hw1: 
	$(CC) NP_project1.cpp $(CXXFLAGS) -o np_hw1

hw1_debug:
	$(CC) NP_project1.cpp $(CXXFLAGS) $(DEBUG_FLAGS) -o np_hw1

hw2:
	$(CC) NP_project2_Concurrent.cpp $(CXXFLAGS) -o np_hw2 -pthread

hw2_debug:
	$(CC) NP_project2_Concurrent.cpp $(CXXFLAGS) $(DEBUG_FLAGS) -o np_hw2 -pthread -g

hw2s:
	$(CC) NP_project2_Single.cpp $(CXXFLAGS) -o np_hw2_s

hw2_debugs:
	$(CC) NP_project2_Single.cpp $(CXXFLAGS) $(DEBUG_FLAGS) -o np_hw2_s -g

clean:
	rm np_*
