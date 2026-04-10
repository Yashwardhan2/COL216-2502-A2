CXX = g++
CXXFLAGS = -std=c++17 -O2

# all your core source files
SRC = preprocessor.cpp

# headers are automatically included

# -----------------------------
# 1. Compile target
# -----------------------------
compile:
	$(CXX) $(CXXFLAGS) $(FILE) $(SRC) -o main

# -----------------------------
# 2. Preprocess target
# -----------------------------
run:
	$(CXX) $(CXXFLAGS) pp_main.cpp preprocessor.cpp -o pp
	./pp $(FILE)

# -----------------------------
# Clean (optional but good)
# -----------------------------
clean:
	rm -f main pp