CXX = g++
CXXFLAGS = -std=c++17 -Wall -I include -pthread

OBJS = src/main.o src/routing.o src/allocation.o
TARGET = app

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
