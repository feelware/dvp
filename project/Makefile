# Makefile para compilar código MPI con OpenCV
CXX = mpic++
CXXFLAGS = -Wall -std=c++11

# Flags automáticas para encontrar OpenCV en el sistema (dentro del contenedor)
LDFLAGS = $(shell pkg-config --libs --cflags opencv4)

TARGET = main
SRC = main.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

clean:
	rm -f $(TARGET)