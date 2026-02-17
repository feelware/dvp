CXX = mpic++
CXXFLAGS = -Wall -std=c++11

LDFLAGS = $(shell pkg-config --libs --cflags opencv4)

TARGET = main
SRC = main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)