CXX = g++
# -MMD -MP: also record which headers each file uses, so editing a .h rebuilds what depends on it
CXXFLAGS = -std=c++17 -Wall -O2 -I include -pthread -MMD -MP
LDLIBS = -lsqlite3

OBJS = src/main.o src/routing.o src/db.o src/schema.o src/password.o src/seed.o \
       src/api.o src/accounts.o src/orders.o src/dispatch.o src/manager.o src/server.o
TARGET = app
DATABASE = data/logistics.db

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(OBJS) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

-include $(OBJS:.o=.d)

clean:
	rm -f $(OBJS) $(OBJS:.o=.d) $(TARGET)

# Deletes the database; the next ./app run creates it again with fresh demo data
reset-db:
	rm -f $(DATABASE) $(DATABASE)-wal $(DATABASE)-shm

.PHONY: clean reset-db
