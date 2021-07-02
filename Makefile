all: rabbitmq

rabbitmq:
	g++ -L/usr/local/lib/ -I/opt/halon/include/ -I/usr/local/include/ -fPIC -shared rabbitmq.cpp -lrabbitmq -o rabbitmq.so
