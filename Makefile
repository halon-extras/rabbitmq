all: rabbitmq

rabbitmq:
	clang++ -L/usr/local/lib/ -I/opt/halon/include/ -I/usr/local/include/ -lrabbitmq -fPIC -shared rabbitmq.cpp -o rabbitmq.so